# AI Coding Directive for Workshop System

Welcome! If you're reading this, you probably have an idea for a new Program Card for the **Music Thing Modular Workshop System** and you want to use AI to help you build it.

That is exactly what this is for.

You don't need to be a professional software engineer to make something musically useful. This project is all about curiosity, sharing, and making fun, playable experiments.

## What is this?

AI tools (like Claude, ChatGPT, Cursor, or Copilot) are incredibly capable, but out of the box they have no idea what the "Workshop System" is. If you just ask an AI to "write a reverb effect for a microcontroller," it will likely give you code that is too heavy, uses the wrong pins, or completely ignores the physical limits of the hardware.

This folder contains a **rules file** — think of it as a **cheat sheet for your AI**.

It teaches your AI assistant:
- The exact hardware we use (and its quirky limitations)
- The `ComputerCard` C++ framework it should use to write code
- Important rules so the AI doesn't break your audio or crash the card
- How to structure and comment code so others can learn from it
- Where to look in the repo for existing examples before inventing new approaches
- How to use git properly so you don't lose work

## How do I use it?

It depends on your AI tool:

- **Claude Code / Claude Projects** — add the directive file as project knowledge, or copy it into your repo as a `CLAUDE.md` file. Claude will read it automatically.
- **Cursor / Copilot / other AI editors** — add it as a rules file or context document (check your tool's docs for the exact method).
- **Chatting with an AI directly** — paste the contents into your conversation before asking it to write Workshop System code.

The key idea: **give your AI this rules file before you ask it to write code**, and the output will be dramatically better — fewer invented APIs, correct audio ranges, proper integer math, and code that respects the 20-microsecond interrupt deadline.

## Do I need to be a coder to use this?

Nope. That's partly the point. If you can describe what you want a program card to do, an AI with this directive can help you build it — even if you've never written C++ before. The directive handles the "how does this hardware actually work" part so you can focus on the "what do I want it to do" part.

## Need help?

Head over to the **Music Thing Workshop Discord** and drop into the `#computer-developers` channel. We'd love to see what you're working on.

## Version

Current version: **V1.8**
