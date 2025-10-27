using System;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using UnityEngine;
using UnityEngine.Events;
using UnityEngine.Networking;

namespace GameDevAI.Dialogue
{
    /// <summary>
    /// Provides a reusable component that lets an NPC in Unity converse with a player by
    /// delegating responses to the Qwen2.5-7B-Instruct language model.
    /// Attach this behaviour to the NPC game object and wire <see cref="OnNpcResponse"/>
    /// into the UI or voice playback layer.
    /// </summary>
    public class QwenNpcDialogue : MonoBehaviour
    {
        [Header("Model Configuration")]
        [SerializeField]
        [Tooltip("Endpoint that exposes an OpenAI-compatible chat completions API for Qwen2.5-7B-Instruct.")]
        private string inferenceEndpoint = "https://api.yourprovider.com/v1/chat/completions";

        [SerializeField]
        [Tooltip("API key required by the inference endpoint. Leave empty if your server does not require authentication.")]
        private string apiKey = string.Empty;

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

        [SerializeField]
        [Tooltip("Optional prompt that defines who the NPC is and how they should behave.")]
        [TextArea(8, 20)]
        private string systemPrompt = DefaultSystemPrompt;

        [Header("Sampling Controls")]
        [SerializeField, Range(0f, 1f)]
        private float temperature = 0.7f;

        [SerializeField, Range(0f, 1f)]
        private float topP = 0.9f;

        [SerializeField, Range(1, 1024)]
        private int maxTokens = 256;

        [Header("Events")]
        public UnityEvent<string> OnNpcResponse;

        /// <summary>
        /// Invoked when the conversation with the LLM fails. Provides the error message so the UI can react.
        /// </summary>
        public UnityEvent<string> OnNpcError;

        private readonly List<ChatMessage> conversation = new List<ChatMessage>();
        private bool isRequestRunning;

        private void Awake()
        {
            if (!string.IsNullOrWhiteSpace(systemPrompt))
            {
                conversation.Add(new ChatMessage("system", systemPrompt));
            }
        }

        /// <summary>
        /// Adds the player's message to the conversation and triggers an asynchronous
        /// request to Qwen to generate the NPC's reply.
        /// </summary>
        /// <param name="playerText">What the player said to the NPC.</param>
        public void SubmitPlayerLine(string playerText)
        {
            if (string.IsNullOrWhiteSpace(playerText))
            {
                return;
            }

            conversation.Add(new ChatMessage("user", playerText));

            if (!isRequestRunning)
            {
                StartCoroutine(SendChatRequest());
            }
        }

        /// <summary>
        /// Clears the stored conversation and re-applies the system prompt if provided.
        /// Useful when the player restarts the dialogue.
        /// </summary>
        public void ResetConversation()
        {
            conversation.Clear();

            if (!string.IsNullOrWhiteSpace(systemPrompt))
            {
                conversation.Add(new ChatMessage("system", systemPrompt));
            }
        }

        /// <summary>
        /// Updates the system prompt at runtime and ensures it is applied to the conversation context.
        /// </summary>
        public void SetSystemPrompt(string prompt)
        {
            systemPrompt = prompt;
            ResetConversation();
        }

        /// <summary>
        /// Allows setting the API key through code, for example after retrieving it from an encrypted store.
        /// </summary>
        public void SetApiKey(string key)
        {
            apiKey = key;
        }

        private IEnumerator SendChatRequest()
        {
            isRequestRunning = true;

            var requestPayload = new ChatCompletionRequest
            {
                model = "Qwen/Qwen2.5-7B-Instruct",
                temperature = temperature,
                top_p = topP,
                max_tokens = maxTokens,
                messages = conversation.ToArray()
            };

            var json = JsonUtility.ToJson(requestPayload, true);
            var requestBytes = Encoding.UTF8.GetBytes(json);

            using var webRequest = new UnityWebRequest(inferenceEndpoint, UnityWebRequest.kHttpVerbPOST)
            {
                uploadHandler = new UploadHandlerRaw(requestBytes),
                downloadHandler = new DownloadHandlerBuffer()
            };

            webRequest.SetRequestHeader("Content-Type", "application/json");

            if (!string.IsNullOrEmpty(apiKey))
            {
                webRequest.SetRequestHeader("Authorization", $"Bearer {apiKey}");
            }

            yield return webRequest.SendWebRequest();

            if (webRequest.result == UnityWebRequest.Result.Success)
            {
                try
                {
                    var response = JsonUtility.FromJson<ChatCompletionResponse>(webRequest.downloadHandler.text);

                    if (response?.choices != null && response.choices.Length > 0 && response.choices[0]?.message != null)
                    {
                        var npcReply = response.choices[0].message.content.Trim();
                        conversation.Add(new ChatMessage("assistant", npcReply));
                        OnNpcResponse?.Invoke(npcReply);
                    }
                    else
                    {
                        HandleError($"Qwen response did not contain any choices. Raw: {webRequest.downloadHandler.text}");
                    }
                }
                catch (Exception exception)
                {
                    HandleError($"Failed to parse Qwen response: {exception.Message}\nRaw: {webRequest.downloadHandler.text}");
                }
            }
            else
            {
                HandleError($"Network error: {webRequest.error}\nResponse: {webRequest.downloadHandler.text}");
            }

            isRequestRunning = false;
        }

        private void HandleError(string message)
        {
            Debug.LogError(message, this);
            OnNpcError?.Invoke(message);
        }

        [Serializable]
        private class ChatMessage
        {
            public string role;
            public string content;

            public ChatMessage(string role, string content)
            {
                this.role = role;
                this.content = content;
            }
        }

        [Serializable]
        private class ChatCompletionRequest
        {
            public string model;
            public float temperature;
            public float top_p;
            public int max_tokens;
            public ChatMessage[] messages;
        }

        [Serializable]
        private class ChatCompletionResponse
        {
            public ChatChoice[] choices;
        }

        [Serializable]
        private class ChatChoice
        {
            public ChatMessage message;
            public string finish_reason;
        }
    }
}
