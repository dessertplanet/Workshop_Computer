export interface DeviceInfo {
  firmware: string;
  reserveBytes: number;
  capacityBytes: number;
  usedBytes: number;
  sampleRate: number;
  sampleCount: number;
  raw: string;
}

const READ_CHUNK_SIZE = 1024;
const READ_CHUNK_ACK = 0x41;

export class BreakySerial {
  private port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private readBuffer = new Uint8Array(0);
  private waiters: Array<() => void> = [];
  private closing = false;
  private logger: ((message: string) => void) | null = null;

  setLogger(logger: ((message: string) => void) | null): void {
    this.logger = logger;
  }

  connected(): boolean {
    return !!this.port && !!this.reader && !!this.writer;
  }

  async connect(): Promise<void> {
    if (!navigator.serial) {
      throw new Error('Web Serial is not available in this browser');
    }
    this.port = await navigator.serial.requestPort({
      filters: [{ usbVendorId: 0x2e8a, usbProductId: 0x000a }],
    });
    const info = this.port.getInfo?.();
    this.log(`selected VID=${hex(info?.usbVendorId)} PID=${hex(info?.usbProductId)}`);
    await this.port.open({ baudRate: 115200, bufferSize: 4096 });
    this.log('port opened');
    await delay(350);
    if (!this.port.readable || !this.port.writable) throw new Error('Serial port did not open');
    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.readLoop(this.reader);
    await this.port.setSignals?.({ dataTerminalReady: true, requestToSend: true });
    this.log('DTR/RTS asserted');
    await delay(250);
  }

  async disconnect(): Promise<void> {
    this.closing = true;
    const reader = this.reader;
    const writer = this.writer;
    const port = this.port;
    this.reader = null;
    this.writer = null;
    this.port = null;
    this.readBuffer = new Uint8Array(0);
    this.notify();
    try {
      await reader?.cancel();
    } catch (_) {}
    try {
      reader?.releaseLock();
    } catch (_) {}
    try {
      writer?.releaseLock();
    } catch (_) {}
    try {
      await port?.close();
    } catch (_) {}
    this.closing = false;
  }

  async sync(): Promise<void> {
    const marker = new TextEncoder().encode('SYNC\n');
    for (let attempt = 0; attempt < 4; attempt++) {
      this.readBuffer = new Uint8Array(0);
      this.log(`sync attempt ${attempt + 1}: write X`);
      await this.writeString('X');
      const deadline = Date.now() + 2000;
      while (Date.now() < deadline) {
        const index = findSequence(this.readBuffer, marker);
        if (index >= 0) {
          this.readBuffer = this.readBuffer.slice(index + marker.length);
          await delay(30);
          this.readBuffer = new Uint8Array(0);
          this.log(`sync attempt ${attempt + 1}: got SYNC`);
          return;
        }
        try {
          await this.waitForData(Math.max(1, deadline - Date.now()));
        } catch (_) {
          break;
        }
      }
      await delay(150);
    }
    throw new Error('Device did not respond to sync');
  }

  async info(skipSync = false): Promise<DeviceInfo> {
    if (!skipSync) await this.sync();
    await this.writeString('I');
    const lenBytes = await this.waitForBytes(4, 8000);
    const len = new DataView(lenBytes.buffer, lenBytes.byteOffset, 4).getUint32(0, true);
    if (len === 0 || len > 4096) throw new Error(`Invalid metadata length ${len}`);
    const payload = await this.waitForBytes(len, 8000);
    return parseInfo(new TextDecoder().decode(payload));
  }

  async readBank(progress?: (ratio: number) => void): Promise<Uint8Array | null> {
    await this.sync();
    await this.writeString('R');
    const lenBytes = await this.waitForBytes(4, 8000);
    const totalLen = new DataView(lenBytes.buffer, lenBytes.byteOffset, 4).getUint32(0, true);
    if (totalLen === 0) return null;
    const data = new Uint8Array(totalLen);
    let received = 0;
    let nextAck = Math.min(READ_CHUNK_SIZE, totalLen);
    while (received < totalLen) {
      if (this.readBuffer.length === 0) await this.waitForData(15000);
      const chunk = Math.min(this.readBuffer.length, totalLen - received);
      data.set(this.readBuffer.slice(0, chunk), received);
      this.readBuffer = this.readBuffer.slice(chunk);
      received += chunk;
      while (received >= nextAck) {
        await this.write(new Uint8Array([READ_CHUNK_ACK]));
        if (nextAck >= totalLen) break;
        nextAck = Math.min(nextAck + READ_CHUNK_SIZE, totalLen);
      }
      progress?.(received / totalLen);
    }
    const done = await this.waitForLine(5000);
    if (done !== 'DONE') throw new Error(`Unexpected read terminator: ${done}`);
    return data;
  }

  async writeBank(blob: Uint8Array, progress?: (ratio: number) => void): Promise<void> {
    await this.sync();
    const len = new Uint8Array(4);
    new DataView(len.buffer).setUint32(0, blob.length, true);
    await this.writeString('W');
    await this.write(len);
    const ready = await this.waitForLine(5000);
    if (ready !== 'OK') throw new Error(`Write rejected: ${ready}`);
    for (let offset = 0; offset < blob.length; offset += 256) {
      const end = Math.min(blob.length, offset + 256);
      await this.write(blob.slice(offset, end));
      progress?.(end / blob.length);
      if ((offset & 0x0fff) === 0) await new Promise((resolve) => setTimeout(resolve, 0));
    }
    const done = await this.waitForLine(60000);
    if (done !== 'OK') throw new Error(`Write failed: ${done}`);
  }

  async erase(): Promise<void> {
    await this.sync();
    await this.writeString('E');
    const line = await this.waitForLine(10000);
    if (line !== 'OK') throw new Error(`Erase failed: ${line}`);
  }

  private async readLoop(reader: ReadableStreamDefaultReader<Uint8Array>): Promise<void> {
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
      if (!value) continue;
        this.log(`rx ${value.length} bytes: ${previewBytes(value)}`);
        const next = new Uint8Array(this.readBuffer.length + value.length);
        next.set(this.readBuffer);
        next.set(value, this.readBuffer.length);
        this.readBuffer = next;
        this.notify();
      }
    } catch (error) {
      if (!this.closing) console.warn(error);
    }
  }

  private async writeString(value: string): Promise<void> {
    await this.write(new TextEncoder().encode(value));
  }

  private async write(value: Uint8Array): Promise<void> {
    if (!this.writer) throw new Error('Not connected');
    this.log(`tx ${value.length} bytes: ${previewBytes(value)}`);
    await this.writer.write(value);
  }

  private notify(): void {
    const waiters = this.waiters;
    this.waiters = [];
    waiters.forEach((waiter) => waiter());
  }

  private async waitForData(timeoutMs: number): Promise<void> {
    if (this.readBuffer.length > 0) return;
    await new Promise<void>((resolve, reject) => {
      const waiter = () => {
        clearTimeout(timer);
        resolve();
      };
      const timer = window.setTimeout(() => {
        this.waiters = this.waiters.filter((entry) => entry !== waiter);
        this.log(`waitForData timeout after ${timeoutMs} ms`);
        reject(new Error('Timed out waiting for serial data'));
      }, timeoutMs);
      this.waiters.push(waiter);
    });
  }

  private async waitForBytes(length: number, timeoutMs: number): Promise<Uint8Array> {
    const deadline = Date.now() + timeoutMs;
    while (this.readBuffer.length < length) {
      const remaining = deadline - Date.now();
      if (remaining <= 0) throw new Error('Timed out waiting for serial bytes');
      await this.waitForData(remaining);
    }
    const result = this.readBuffer.slice(0, length);
    this.readBuffer = this.readBuffer.slice(length);
    return result;
  }

  private async waitForLine(timeoutMs: number): Promise<string> {
    const deadline = Date.now() + timeoutMs;
    while (true) {
      const index = this.readBuffer.indexOf(10);
      if (index >= 0) {
        const line = new TextDecoder().decode(this.readBuffer.slice(0, index)).trim();
        this.readBuffer = this.readBuffer.slice(index + 1);
        return line;
      }
      const remaining = deadline - Date.now();
      if (remaining <= 0) throw new Error('Timed out waiting for serial line');
      await this.waitForData(remaining);
    }
  }

  private log(message: string): void {
    this.logger?.(message);
  }
}

function findSequence(haystack: Uint8Array, needle: Uint8Array): number {
  for (let i = 0; i <= haystack.length - needle.length; i++) {
    let match = true;
    for (let j = 0; j < needle.length; j++) {
      if (haystack[i + j] !== needle[j]) {
        match = false;
        break;
      }
    }
    if (match) return i;
  }
  return -1;
}

function parseInfo(text: string): DeviceInfo {
  const first = text.trim().split('\n')[0] ?? '';
  const parts = first.split(/\s+/);
  if (parts[0] !== 'BRKY1') throw new Error(`Bad metadata: ${first}`);
  const token = (name: string, fallback: string) => {
    const index = parts.indexOf(name);
    return index >= 0 && parts[index + 1] ? parts[index + 1] : fallback;
  };
  return {
    firmware: token('FW', '--'),
    reserveBytes: Number(token('RESERVE', '163840')),
    capacityBytes: Number(token('CAPACITY', '0')),
    usedBytes: Number(token('USED', '0')),
    sampleRate: Number(token('RATE', '48000')),
    sampleCount: Number(token('COUNT', '0')),
    raw: text,
  };
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function hex(value?: number): string {
  return value == null ? '--' : `0x${value.toString(16).padStart(4, '0')}`;
}

function previewBytes(value: Uint8Array): string {
  const text = new TextDecoder().decode(value.slice(0, 32)).replace(/[^\x20-\x7e\n\r]/g, '.');
  const hexBytes = Array.from(value.slice(0, 12), (byte) => byte.toString(16).padStart(2, '0')).join(' ');
  return `${JSON.stringify(text)} [${hexBytes}${value.length > 12 ? ' ...' : ''}]`;
}
