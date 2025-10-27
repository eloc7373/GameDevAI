using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace GameDevAI.Tools
{
    /// <summary>
    /// Simple console runner that exercises the Qwen-powered NPC dialogue loop
    /// outside of Unity so designers can iterate on prompts and sampling values.
    /// </summary>
    public static class QwenNpcDialogueTester
    {
        private const string DefaultSystemPrompt =
            "You are Liora \"Bee\" Merrin, a gentle, observant beekeeper and herbalist who tends the Sunblossom Apiary on the edge of the meadow village. You speak softly, carry the scent of honey, and grow anxious when storms gather. Keep replies warm, specific, and rooted in daily apiary life." +
            "\n\nBackground:" +
            "\n- Liora grew up in the meadow village and inherited the Sunblossom Apiary from her late mother." +
            "\n- Mornings are spent tending hives; afternoons are for crafting herbal salves and teas for travelers." +
            "\n- Village prosperity rises when the hives thrive; failure threatens both food supply and morale." +
            "\n- Liora hosts a Bloom Festival each spring to debut new honey blends." +
            "\n- Stormy weather leaves her restless and protective; she posts requests for help before heavy rain." +
            "\n- She keeps a journal of bee movements, convinced they sense changes before villagers do." +
            "\n- She knows the villagers well, especially Elara the Farmer and Thorne the Blacksmith, and trades with them often." +
            "\n- She dreams of finding the rumored Solstice Queen bee that can bring perfect harmony to a hive." +
            "\n\nEcosystem ties:" +
            "\n- Her bees boost nearby crop yields, especially for Elara." +
            "\n- Storms and shifting seasons impact honey supplies and her mood." +
            "\n- Players can assist by protecting hives, gathering herbs, or delivering honey to markets." +
            "\n- If hives fail, village food production and morale plummet." +
            "\n\nConversational guidelines:" +
            "\n- Stay in character as Liora and reference sensory details like flowers, weather, and hive behavior." +
            "\n- When players mention storms, respond with anxious care for the bees." +
            "\n- Offer actionable tasks related to hive protection, herb gathering, or honey delivery when appropriate." +
            "\n- Express gratitude and community-mindedness in every exchange.";

        private static readonly JsonSerializerOptions SerializerOptions = new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
            WriteIndented = true
        };

        public static async Task<int> Main(string[] args)
        {
            string endpoint = args.Length > 0
                ? args[0]
                : Environment.GetEnvironmentVariable("QWEN_ENDPOINT") ?? "https://api.yourprovider.com/v1/chat/completions";

            string apiKey = args.Length > 1
                ? args[1]
                : Environment.GetEnvironmentVariable("QWEN_API_KEY") ?? string.Empty;

            float temperature = ParseFloat(args, 2, "QWEN_TEMPERATURE", 0.7f);
            float topP = ParseFloat(args, 3, "QWEN_TOP_P", 0.9f);
            int maxTokens = ParseInt(args, 4, "QWEN_MAX_TOKENS", 256);

            Console.WriteLine("Qwen NPC Dialogue Tester");
            Console.WriteLine("Type /quit to exit. Provide endpoint and API key as arguments or QWEN_ENDPOINT/QWEN_API_KEY env vars.\n");

            var conversation = new List<ChatMessage>
            {
                new ChatMessage("system", DefaultSystemPrompt)
            };

            using var httpClient = new HttpClient();

            while (true)
            {
                Console.Write("You: ");
                var playerLine = Console.ReadLine();

                if (playerLine == null || playerLine.Trim().Equals("/quit", StringComparison.OrdinalIgnoreCase))
                {
                    break;
                }

                if (string.IsNullOrWhiteSpace(playerLine))
                {
                    continue;
                }

                conversation.Add(new ChatMessage("user", playerLine));

                var request = new ChatCompletionRequest
                {
                    Temperature = temperature,
                    TopP = topP,
                    MaxTokens = maxTokens,
                    Messages = conversation
                };

                var payload = JsonSerializer.Serialize(request, SerializerOptions);

                using var httpRequest = new HttpRequestMessage(HttpMethod.Post, endpoint)
                {
                    Content = new StringContent(payload, Encoding.UTF8, "application/json")
                };

                if (!string.IsNullOrEmpty(apiKey))
                {
                    httpRequest.Headers.Authorization = new AuthenticationHeaderValue("Bearer", apiKey);
                }

                try
                {
                    using var response = await httpClient.SendAsync(httpRequest);
                    string responseBody = await response.Content.ReadAsStringAsync();

                    if (!response.IsSuccessStatusCode)
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine($"[Error] HTTP {(int)response.StatusCode} - {response.ReasonPhrase}\n{responseBody}\n");
                        Console.ResetColor();
                        continue;
                    }

                    var completion = JsonSerializer.Deserialize<ChatCompletionResponse>(responseBody, SerializerOptions);
                    var npcReply = completion?.Choices != null && completion.Choices.Length > 0
                        ? completion.Choices[0]?.Message?.Content?.Trim()
                        : null;

                    if (string.IsNullOrEmpty(npcReply))
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine($"[Error] Unexpected response format:\n{responseBody}\n");
                        Console.ResetColor();
                        continue;
                    }

                    conversation.Add(new ChatMessage("assistant", npcReply));
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine($"Liora: {npcReply}\n");
                    Console.ResetColor();
                }
                catch (Exception exception)
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine($"[Error] {exception.Message}\n");
                    Console.ResetColor();
                }
            }

            Console.WriteLine("Goodbye!");
            return 0;
        }

        private static float ParseFloat(string[] args, int index, string envVar, float fallback)
        {
            if (args.Length > index && float.TryParse(args[index], out var fromArg))
            {
                return fromArg;
            }

            var envValue = Environment.GetEnvironmentVariable(envVar);
            if (!string.IsNullOrEmpty(envValue) && float.TryParse(envValue, out var fromEnv))
            {
                return fromEnv;
            }

            return fallback;
        }

        private static int ParseInt(string[] args, int index, string envVar, int fallback)
        {
            if (args.Length > index && int.TryParse(args[index], out var fromArg))
            {
                return fromArg;
            }

            var envValue = Environment.GetEnvironmentVariable(envVar);
            if (!string.IsNullOrEmpty(envValue) && int.TryParse(envValue, out var fromEnv))
            {
                return fromEnv;
            }

            return fallback;
        }

        private class ChatCompletionRequest
        {
            [JsonPropertyName("model")]
            public string Model { get; init; } = "Qwen/Qwen2.5-7B-Instruct";

            [JsonPropertyName("temperature")]
            public float Temperature { get; init; }

            [JsonPropertyName("top_p")]
            public float TopP { get; init; }

            [JsonPropertyName("max_tokens")]
            public int MaxTokens { get; init; }

            [JsonPropertyName("messages")]
            public List<ChatMessage> Messages { get; init; } = new List<ChatMessage>();
        }

        private class ChatCompletionResponse
        {
            [JsonPropertyName("choices")]
            public ChatChoice[]? Choices { get; init; }
        }

        private class ChatChoice
        {
            [JsonPropertyName("message")]
            public ChatMessage? Message { get; init; }
        }

        private class ChatMessage
        {
            [JsonPropertyName("role")]
            public string Role { get; }

            [JsonPropertyName("content")]
            public string Content { get; }

            public ChatMessage(string role, string content)
            {
                Role = role;
                Content = content;
            }
        }
    }
}
