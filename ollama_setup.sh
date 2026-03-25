#!/bin/bash

# Start Ollama in the background.
#OLLAMA_CONTEXT_LENGTH=40000 OLLAMA_DEBUG=1 /bin/ollama serve &
OLLAMA_CONTEXT_LENGTH=128000 OLLAMA_DEBUG=1 /bin/ollama serve &
# Record Process ID.
pid=$!

# Pause for Ollama to start.
sleep 5

echo "🔴 Retrieve LLAMA3 model..."
#ollama pull gemma3:27b
#ollama pull gpt-oss:20b
echo "🟢 Done!"

# Wait for Ollama process to finish.
wait $pid
