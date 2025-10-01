# smartTerm

`smart_terminal` is a small C program that lets you collaborate with an Ollama-compatible language model directly from your shell. You describe a goal, the application forwards a structured prompt to the configured model, and then automatically follows the model's suggested actions (running commands, writing files, or asking for clarification) until the model reports that the task is complete.

## Requirements

The project relies on:

- GNU Make and GCC
- `pkg-config`
- Development headers for libcurl and json-c

Run `./configure` to verify that the necessary build tools and libraries are present before attempting to compile the client.

## Building on Linux

```bash
./configure
make
```

The resulting `smart_terminal` binary can optionally be installed to `/usr/local/bin` with `sudo make install`. Use `make clean` to remove the compiled executable.

## Configuring the AI backend

Edit `smart_terminal.c` if you need to change either of the following macros:

- `OLLAMA_URL` – the full endpoint (including `/api/generate`) for your Ollama or compatible server.
- `OLLAMA_MODEL` – the model identifier to request from the server.

Rebuild the program after modifying these values.

## Usage

1. Launch the program with `./smart_terminal`.
2. When prompted, enter your goal wrapped in double quotes, for example: `"check disk usage"`.
3. The agent will display each step as it runs commands or writes files on your behalf. Answer any follow-up questions directly in the terminal.
4. Type `exit` or `quit` at the main prompt to leave the application.

If a command or file write repeats without making progress, the client will automatically stop to protect your system. When this happens you can provide additional guidance or end the session.

## License

See [LICENSE](./LICENSE) for full licensing information.
