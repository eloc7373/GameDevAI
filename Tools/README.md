# Qwen NPC Dialogue Tester

This folder contains a small .NET 8 console application for rehearsing the
Qwen2.5-7B-Instruct powered NPC dialogue loop outside of Unity.

## Prerequisites

Install the .NET 8 SDK on your development machine. On systems with internet
access you can install it by following the instructions at
<https://learn.microsoft.com/dotnet/core/install/>.

## Running the tester

```bash
dotnet run --project Tools/QwenNpcDialogueTester.csproj \
  https://api.yourprovider.com/v1/chat/completions YOUR_API_KEY
```

If you omit the endpoint or key they will be read from the `QWEN_ENDPOINT` and
`QWEN_API_KEY` environment variables. Optional arguments for temperature,
`top_p`, and `max_tokens` can be supplied as additional parameters or matching
environment variables.

Once running, enter player dialogue at the prompt. Type `/quit` to exit.
