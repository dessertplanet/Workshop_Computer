# AI Coding Directive for Workshop System

**A cheat sheet that teaches AI assistants how to write code for the Workshop Computer.**

## What is this?

The Workshop System Computer has quirks — specific hardware limits, a particular way its code framework works, and real-world gotchas that only become obvious after you've hit them. That's a lot for anyone (or any AI) to keep track of.

This directive file is a structured reference document that AI coding tools (like Claude, Cursor, Copilot, etc.) can read before they start writing code for you. It gives them the context and knowledge they need to produce code that actually builds, runs, and sounds right on real hardware.

Think of it like handing a new bandmate the setlist, the tuning, and a note that says "don't touch the red cable."

## What's in it?

- **Platform basics** — what the hardware is, what it can and can't do
- **The ComputerCard API** — how to talk to knobs, jacks, LEDs, and audio I/O
- **Hardware errata** — known chip-level quirks (like the ADC dead zones on every RP2040)
- **DSP guidance** — performance budgets, integer math patterns, what fits in 20 microseconds
- **Contribution standards** — how to structure and comment code so others can learn from it
- **Coding Methods for best result** — Instructions to look to other cards for examples, using GIT for version control and other tips to steer the AI to help you get better results and have a smoother coding experience.

## How do I use it?

It depends on your AI tool:

- **Claude Code / Claude Projects** — add the directive file as project knowledge or copy it into your repo as a `CLAUDE.md` file. Claude will read it automatically.
- **Cursor / Copilot / other AI editors** — add it as a rules file or context document (check your tool's docs for the exact method).
- **Chatting with an AI directly** — paste the contents into your conversation and explain that these are the rules for this project before asking it to write Workshop System code.

The key idea: **give your AI this rules file before you ask it to write code**, and the output will be dramatically better — fewer invented APIs, correct audio ranges, proper integer math, and code that respects the 20-microsecond interrupt deadline.

## Do I need to be a coder to use this?

Nope. That's partly the point. If you can describe what you want a program card to do, an AI with this directive can help you build it — even if you've never written C++ before. The directive handles the "how does this hardware actually work" part so you can focus on the "what do I want it to do" part.

## Version

Current version: **V1.8**
