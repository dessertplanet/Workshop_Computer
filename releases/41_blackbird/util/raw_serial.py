import serial
import serial.tools.list_ports
import sys
import threading
import time

def find_blackbird_port():
    """Find the Blackbird/crow device automatically"""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        # Look for crow device or STMicroelectronics VID
        if 'crow' in port.description.lower() or port.vid == 0x0483:
            return port.device
    return None

def read_serial(ser, stop_event):
    """Thread function to continuously read from serial port"""
    while not stop_event.is_set():
        try:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                
                # Show with visible escape characters
                readable = data.decode('utf-8', errors='replace')
                readable = readable.replace('\r', '\\r').replace('\n', '\\n').replace('\t', '\\t')
                print(f"RX:  {readable}")
                
        except Exception as e:
            if not stop_event.is_set():
                print(f"\nRead error: {e}")
            break
        time.sleep(0.01)  # Small delay to prevent CPU spinning

def main():
    # Try to find the port automatically
    port = find_blackbird_port()
    
    if not port:
        print("Available serial ports:")
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device}: {p.description} (VID: {hex(p.vid) if p.vid else 'N/A'})")
        
        if len(sys.argv) > 1:
            port = sys.argv[1]
        else:
            port = input("\nEnter port name (e.g., /dev/tty.usbmodem1234 or COM3): ")
    else:
        print(f"Found Blackbird at: {port}")
    
    try:
        # Open serial port
        ser = serial.Serial(port, 115200, timeout=0.1)
        print(f"Connected to {port} at 115200 baud")
        print("Type commands and press Enter to send (Ctrl+C to exit)")
        print("Commands are sent with \\n appended")
        print("-" * 60)
        
        # Create stop event for thread
        stop_event = threading.Event()
        
        # Start reader thread
        reader_thread = threading.Thread(target=read_serial, args=(ser, stop_event), daemon=True)
        reader_thread.start()
        
        # Main input loop
        print("> ", end='', flush=True)
        while True:
            try:
                user_input = input()
                if user_input:
                    # Send with \n appended
                    message = user_input + '\n'
                    ser.write(message.encode('utf-8'))
                    print(f"TX: {repr(message)}")
                print("> ", end='', flush=True)
            except EOFError:
                break
                
    except serial.SerialException as e:
        print(f"Error: {e}")
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        if 'stop_event' in locals():
            stop_event.set()
        if 'reader_thread' in locals():
            reader_thread.join(timeout=1)
        if 'ser' in locals() and ser.is_open:
            ser.close()

if __name__ == "__main__":
    main()