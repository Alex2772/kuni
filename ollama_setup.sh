#!/bin/bash

# Start Ollama in the background.
OLLAMA_KEEP_ALIVE=1m OLLAMA_CONTEXT_LENGTH=65536 /bin/ollama serve &
# Record Process ID.
pid=$!

# Pause for Ollama to start.
sleep 5

echo "🔴 Retrieve LLAMA3 model..."
ollama pull qwen3-embedding
ollama pull qwen3.5:9b
echo "🟢 Done!"

# Wait for Ollama process to finish.
wait $pid
