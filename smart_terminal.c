#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>

// Configuration for your AI Server
#define OLLAMA_URL "http://192.168.50.5:11434/api/generate"
#define OLLAMA_MODEL "gpt-oss:latest"

// *** THE FINAL PROMPT: A CLEARER, PRINCIPLE-BASED "CONSTITUTION" ***
#define PROMPT_TEMPLATE "You are a methodical Linux agent. Your goal is to accomplish the user's request by creating and executing a logical sequence of actions. Your response MUST be a single, raw JSON object for the next action.\n\n**Your Constitution:**\n1.  **Think Step-by-Step:** Break down complex goals into a series of simple, single-purpose steps. Do not chain unrelated commands.\n2.  **Proceed Logically:** After creating a resource (like a file), the next logical step is to USE it (e.g., compile it). After installing a tool, the next logical step is to USE that tool.\n3.  **Use Privileges Correctly:** Always prepend 'sudo' to any command that requires root privileges (like 'apt', 'dpkg', 'systemctl').\n4.  **Stay Focused:** Always refer back to the user's original goal to guide your next step.\n\n**USER'S GOAL:** \"%s\"\n\n**LAST STEP'S OUTPUT:**\n---\n%s\n---\n\nBased on your constitution, the user's goal, and the last step's output, determine the single next action. Only use 'answer_user' when the goal is 100%% complete.\n\n**Actions:**\n1. `execute_command`: {\"action\": \"execute_command\", \"parameters\": {\"command\": \"<shell_command>\"}}\n2. `write_file`: {\"action\": \"write_file\", \"parameters\": {\"filename\": \"<file_name>\", \"content\": \"<file_content>\"}}\n3. `ask_user`: {\"action\": \"ask_user\", \"parameters\": {\"question\": \"<your question>\"}}\n4. `answer_user`: {\"action\": \"answer_user\", \"parameters\": {\"answer\": \"<final report>\"}}\n\nGenerate the JSON for the single next action: "

typedef struct { char *action, *command, *filename, *content, *answer, *question; } AIAction;
struct MemoryStruct { char *memory; size_t size; };

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) { printf("Error: not enough memory\n"); return 0; }
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

char* execute_and_capture(const char* command) {
    FILE *fp;
    char buffer[1024];
    char *output = malloc(1);
    output[0] = '\0';
    size_t output_size = 1;
    char full_command[2048];
    snprintf(full_command, sizeof(full_command), "%s 2>&1", command);
    fp = popen(full_command, "r");
    if (fp == NULL) { fprintf(stderr, "Failed to run command\n"); return output; }
    printf("--- Output ---\n");
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        printf("%s", buffer);
        size_t buffer_len = strlen(buffer);
        char *ptr = realloc(output, output_size + buffer_len);
        if(!ptr) { fprintf(stderr, "Failed to realloc memory\n"); break; }
        output = ptr;
        strcat(output, buffer);
        output_size += buffer_len;
    }
    printf("--------------\n");
    pclose(fp);
    return output;
}

void parse_ollama_action(const char *json_string, AIAction *ai_action) {
    struct json_object *parsed_json, *response_obj;
    memset(ai_action, 0, sizeof(AIAction));
    parsed_json = json_tokener_parse(json_string);
    if (!parsed_json) return;
    if (json_object_object_get_ex(parsed_json, "response", &response_obj)) {
        const char *response_str = json_object_get_string(response_obj);
        struct json_object *action_json = json_tokener_parse(response_str);
        if (!action_json) { json_object_put(parsed_json); return; }
        struct json_object *type_obj, *params_obj, *temp_obj;
        if (json_object_object_get_ex(action_json, "action", &type_obj)) {
            ai_action->action = strdup(json_object_get_string(type_obj));
        }
        if (ai_action->action && json_object_object_get_ex(action_json, "parameters", &params_obj)) {
            if (strcmp(ai_action->action, "execute_command") == 0) {
                if (json_object_object_get_ex(params_obj, "command", &temp_obj))
                    ai_action->command = strdup(json_object_get_string(temp_obj));
            } else if (strcmp(ai_action->action, "write_file") == 0) {
                if (json_object_object_get_ex(params_obj, "filename", &temp_obj))
                    ai_action->filename = strdup(json_object_get_string(temp_obj));
                if (json_object_object_get_ex(params_obj, "content", &temp_obj))
                    ai_action->content = strdup(json_object_get_string(temp_obj));
            } else if (strcmp(ai_action->action, "answer_user") == 0) {
                if (json_object_object_get_ex(params_obj, "answer", &temp_obj))
                    ai_action->answer = strdup(json_object_get_string(temp_obj));
            } else if (strcmp(ai_action->action, "ask_user") == 0) {
                if (json_object_object_get_ex(params_obj, "question", &temp_obj))
                    ai_action->question = strdup(json_object_get_string(temp_obj));
            }
        }
        json_object_put(action_json);
    }
    json_object_put(parsed_json);
}

void get_ai_action(const char* full_prompt, AIAction *ai_action) {
    CURL *curl;
    struct MemoryStruct chunk = { .memory = malloc(1), .size = 0 };
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl) {
        json_object *jobj = json_object_new_object();
        json_object_object_add(jobj, "model", json_object_new_string(OLLAMA_MODEL));
        json_object_object_add(jobj, "prompt", json_object_new_string(full_prompt));
        json_object_object_add(jobj, "stream", json_object_new_boolean(0));
        const char *json_payload = json_object_to_json_string(jobj);
        struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, OLLAMA_URL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        printf("🧠 Thinking... "); fflush(stdout);
        CURLcode res = curl_easy_perform(curl);
        printf("Done.\n");
        if (res == CURLE_OK) parse_ollama_action(chunk.memory, ai_action);
        else fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        json_object_put(jobj);
    }
    curl_global_cleanup();
    free(chunk.memory);
}

void free_ai_action(AIAction *action) {
    if (action->action) free(action->action);
    if (action->command) free(action->command);
    if (action->filename) free(action->filename);
    if (action->content) free(action->content);
    if (action->answer) free(action->answer);
    if (action->question) free(action->question);
}

int main() {
    char input[4096];
    printf("Welcome to Smart Agent Terminal ✨\n");
    printf("State your goal starting with '\"' (e.g., \"is a webserver running?\").\n");
    printf("--------------------------------------------------------------------------------\n");

    while (1) {
        printf(">> ");
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) continue;
        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) break;

        if (input[0] == '"') {
            char *goal = strdup(input + 1);
            char *last_char = strrchr(goal, '"');
            if (last_char) *last_char = '\0';

            char *context = strdup("No commands executed yet. This is the first step.");
            
            const int history_size = 3;
            char *command_history[history_size];
            for(int i=0; i<history_size; i++) command_history[i] = NULL;

            for (int loop_count = 0; loop_count < 15; loop_count++) { // Safety break
                char full_prompt[16384];
                snprintf(full_prompt, sizeof(full_prompt), PROMPT_TEMPLATE, goal, context);
                
                AIAction action;
                get_ai_action(full_prompt, &action);

                free(context);
                context = NULL;

                if (!action.action) {
                    printf("🤖 AI failed to provide a valid action. Aborting.\n");
                    break;
                }

                if (strcmp(action.action, "execute_command") == 0 && action.command) {
                    int is_stuck = 0;
                    for(int i=0; i<history_size; i++) {
                        if(command_history[i] && strcmp(command_history[i], action.command) == 0) {
                            is_stuck = 1;
                            break;
                        }
                    }
                    if(is_stuck) {
                        printf("🤖 AI is stuck in a loop. Aborting automatic execution.\n");
                        free_ai_action(&action);
                        break;
                    }
                    if(command_history[history_size-1]) free(command_history[history_size-1]);
                    for(int i=history_size-1; i > 0; i--) command_history[i] = command_history[i-1];
                    command_history[0] = strdup(action.command);
                    printf("🤖 Step %d: Running command: \033[1;33m%s\033[0m\n", loop_count + 1, action.command);
                    context = execute_and_capture(action.command);

                } else if (strcmp(action.action, "write_file") == 0 && action.filename && action.content) {
                    printf("🤖 Step %d: Writing to file: \033[1;33m%s\033[0m\n", loop_count + 1, action.filename);
                    FILE *fp = fopen(action.filename, "w");
                    if (fp) {
                        fprintf(fp, "%s", action.content);
                        fclose(fp);
                        context = strdup("File written successfully.");
                        printf("✅ %s\n", context);
                    } else {
                        context = strdup("Error opening file for writing.");
                        fprintf(stderr, "Error: %s\n", context);
                    }
                } else if (strcmp(action.action, "ask_user") == 0 && action.question) {
                    printf("🤖 \033[1;34mAI Question: %s\033[0m\n", action.question);
                    printf("Your reply: ");
                    char reply[1024];
                    if(fgets(reply, sizeof(reply), stdin) != NULL) {
                        reply[strcspn(reply, "\n")] = 0;
                        context = strdup(reply);
                    } else {
                        context = strdup("User provided no reply.");
                    }
                }
                else if (strcmp(action.action, "answer_user") == 0 && action.answer) {
                    printf("✅ \033[1;32mAI Final Answer: %s\033[0m\n", action.answer);
                    free_ai_action(&action);
                    break; 
                } else {
                    printf("🤖 AI returned an invalid or empty action. Aborting.\n");
                    free_ai_action(&action);
                    break;
                }
                
                free_ai_action(&action);
            }

            if (context) free(context);
            free(goal);
            for(int i=0; i<history_size; i++) if(command_history[i]) free(command_history[i]);
        } else {
            system(input);
        }
    }
    printf("\nExiting Smart Agent. Goodbye!\n");
    return 0;
}
