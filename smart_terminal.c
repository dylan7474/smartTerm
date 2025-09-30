#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>

// Configuration for your AI Server
#define OLLAMA_URL "http://192.168.50.5:11434/api/generate"
#define OLLAMA_MODEL "gpt-oss:latest"

// Multi-step prompt for generating a full plan
#define PROMPT_PREFIX "You are a task-oriented AI assistant for a Linux terminal. Your response MUST be a single, raw JSON array of action objects. Do not include any other text or markdown. Each object in the array represents a step to be executed in sequence.\n\nThere are two possible actions:\n1.  **execute_command**: To run a shell command.\n    - JSON format: {\"action\": \"execute_command\", \"parameters\": {\"command\": \"<shell_command>\"}}\n\n2.  **write_file**: To create or overwrite a file with specific content.\n    - JSON format: {\"action\": \"write_file\", \"parameters\": {\"filename\": \"<file_name>\", \"content\": \"<file_content>\"}}\n\nExample: User asks \"create and run a hello world C program\". You respond with:\n[{\"action\":\"write_file\",\"parameters\":{\"filename\":\"hello.c\",\"content\":\"#include <stdio.h>\\nint main() { printf(\\\"Hello, World!\\\\n\\\"); return 0; }\"}},{\"action\":\"execute_command\",\"parameters\":{\"command\":\"gcc hello.c -o hello\"}},{\"action\":\"execute_command\",\"parameters\":{\"command\":\"./hello\"}}]\n\nNow, generate the JSON array for the following user request: "

typedef struct { char *action, *command, *filename, *content; } AIAction;
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

int parse_ollama_actions(const char *json_string, AIAction **actions_array) {
    struct json_object *parsed_json, *response_obj;
    int num_actions = 0;
    parsed_json = json_tokener_parse(json_string);
    if (!parsed_json) return 0;

    if (json_object_object_get_ex(parsed_json, "response", &response_obj)) {
        const char *response_str = json_object_get_string(response_obj);
        struct json_object *actions_json = json_tokener_parse(response_str);
        
        if (actions_json && json_object_is_type(actions_json, json_type_array)) {
            num_actions = json_object_array_length(actions_json);
            *actions_array = (AIAction *)calloc(num_actions, sizeof(AIAction));

            for (int i = 0; i < num_actions; i++) {
                struct json_object *action_obj = json_object_array_get_idx(actions_json, i);
                struct json_object *type_obj, *params_obj, *temp_obj;
                if (json_object_object_get_ex(action_obj, "action", &type_obj)) {
                    (*actions_array)[i].action = strdup(json_object_get_string(type_obj));
                }
                if (json_object_object_get_ex(action_obj, "parameters", &params_obj)) {
                    if (strcmp((*actions_array)[i].action, "execute_command") == 0) {
                        if (json_object_object_get_ex(params_obj, "command", &temp_obj))
                            (*actions_array)[i].command = strdup(json_object_get_string(temp_obj));
                    } else if (strcmp((*actions_array)[i].action, "write_file") == 0) {
                        if (json_object_object_get_ex(params_obj, "filename", &temp_obj))
                            (*actions_array)[i].filename = strdup(json_object_get_string(temp_obj));
                        if (json_object_object_get_ex(params_obj, "content", &temp_obj))
                            (*actions_array)[i].content = strdup(json_object_get_string(temp_obj));
                    }
                }
            }
        }
        if (actions_json) json_object_put(actions_json);
    }
    json_object_put(parsed_json);
    return num_actions;
}

int get_ai_actions(const char* prompt, AIAction **actions_array) {
    CURL *curl;
    int num_actions = 0;
    struct MemoryStruct chunk = { .memory = malloc(1), .size = 0 };

    curl = curl_easy_init();
    if (curl) {
        char full_prompt[8192];
        snprintf(full_prompt, sizeof(full_prompt), "%s\"%s\"", PROMPT_PREFIX, prompt);

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

        if (res == CURLE_OK) num_actions = parse_ollama_actions(chunk.memory, actions_array);
        else fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        json_object_put(jobj);
    }
    free(chunk.memory);
    return num_actions;
}

void free_ai_actions(AIAction *actions, int num_actions) {
    if (!actions) return;
    for (int i = 0; i < num_actions; i++) {
        if (actions[i].action) free(actions[i].action);
        if (actions[i].command) free(actions[i].command);
        if (actions[i].filename) free(actions[i].filename);
        if (actions[i].content) free(actions[i].content);
    }
    free(actions);
}

// *** MAIN LOOP IS FULLY AUTOMATIC - NO USER PROMPT ***
int main() {
    char input[4096];
    printf("Welcome to Smart Terminal (Automatic Mode) ✨\n");
    printf("State your goal starting with '\"' (e.g., \"compile and run a hello world C program\").\n");
    printf("--------------------------------------------------------------------------------\n");

    while (1) {
        printf(">> ");
        if (fgets(input, sizeof(input), stdin) == NULL) break;

        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) continue;
        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) break;

        if (input[0] == '"') {
            char *prompt = input + 1;
            char *last_char = strrchr(prompt, '"');
            if (last_char) *last_char = '\0';

            AIAction *actions = NULL;
            int num_actions = get_ai_actions(prompt, &actions);

            if (num_actions > 0) {
                printf("🤖 AI has planned %d step(s). Executing automatically...\n", num_actions);

                for (int i = 0; i < num_actions; i++) {
                    printf("---\n");
                    printf("[Step %d/%d]: ", i + 1, num_actions);

                    if (strcmp(actions[i].action, "execute_command") == 0 && actions[i].command) {
                        printf("Running command: \033[1;33m%s\033[0m\n", actions[i].command);
                        system(actions[i].command);
                    } else if (strcmp(actions[i].action, "write_file") == 0 && actions[i].filename && actions[i].content) {
                        printf("Writing to file: \033[1;33m%s\033[0m\n", actions[i].filename);
                        FILE *fp = fopen(actions[i].filename, "w");
                        if (fp) {
                            fprintf(fp, "%s", actions[i].content);
                            fclose(fp);
                            printf("✅ File '%s' created.\n", actions[i].filename);
                        } else {
                            fprintf(stderr, "Error opening file '%s'.\n", actions[i].filename);
                        }
                    }
                }
                printf("--- \n✅ Plan finished.\n");
            } else {
                printf("Sorry, the AI could not determine a plan.\n");
            }
            free_ai_actions(actions, num_actions);
        } else {
            system(input);
        }
    }
    printf("\nExiting Smart Terminal. Goodbye!\n");
    return 0;
}
