#include "pc_agent.h"
#include <gio/gio.h>
#include <string.h>

/* Protocol tests use tiny in-memory image markers. They never open a window,
 * capture the desktop, execute an action, or contact a model provider. */
static const PcSession frame = {
    .image = "data:image/png;base64,fixture-one",
    .image_width = 800, .image_height = 600
};

static char *encode(JsonNode *n) {
    JsonGenerator *g = json_generator_new();
    json_generator_set_root(g, n);
    char *result = json_generator_to_data(g, NULL);
    g_object_unref(g);
    return result;
}

static char *encode_object(JsonObject *o) {
    JsonNode *n = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(n, o);
    char *result = encode(n);
    json_node_free(n);
    return result;
}

static JsonParser *parse(const char *text) {
    JsonParser *p = json_parser_new();
    GError *e = NULL;
    g_assert_true(json_parser_load_from_data(p, text, -1, &e));
    g_assert_no_error(e);
    g_assert_true(JSON_NODE_HOLDS_OBJECT(json_parser_get_root(p)));
    return p;
}

static JsonObject *action_object(guint number, const char *kind,
                                const char *verification, const char *outcome) {
    JsonObject *o = json_object_new();
    json_object_set_string_member(o, "action", kind);
    json_object_set_string_member(o, "reason", "Operate the disposable test field");
    json_object_set_string_member(o, "text", !strcmp(kind, "type") ? "hello" : "");
    json_object_set_int_member(o, "x", !strcmp(kind, "click") ? 80 : 0);
    json_object_set_int_member(o, "y", !strcmp(kind, "click") ? 100 : 0);
    json_object_set_int_member(o, "delta", 0);
    json_object_set_int_member(o, "frame", number);
    json_object_set_string_member(o, "observation", "The test field is visible");
    json_object_set_string_member(o, "expected", !strcmp(kind, "done") ? "" : "The requested state becomes visible");
    json_object_set_string_member(o, "verification", verification);
    json_object_set_string_member(o, "evidence", "The screenshot contains the test field");
    json_object_set_string_member(o, "outcome", outcome);
    return o;
}

static char *chat_body(JsonObject *action) {
    char *arguments = encode_object(action);
    JsonObject *root = json_object_new(), *choice = json_object_new(), *message = json_object_new();
    JsonArray *choices = json_array_new();
    json_object_set_string_member(message, "content", arguments);
    json_object_set_object_member(choice, "message", message);
    json_object_set_string_member(choice, "finish_reason", "stop");
    json_array_add_object_element(choices, choice);
    json_object_set_array_member(root, "choices", choices);
    char *result = encode_object(root);
    json_object_unref(root); g_free(arguments);
    return result;
}

static JsonObject *tool_item(JsonObject *action, const char *call_id) {
    JsonObject *tool = json_object_new();
    char *arguments = encode_object(action);
    json_object_set_string_member(tool, "type", "function_call");
    json_object_set_string_member(tool, "id", "fc_fixture");
    json_object_set_string_member(tool, "call_id", call_id);
    json_object_set_string_member(tool, "name", "desktop_action");
    json_object_set_string_member(tool, "status", "completed");
    json_object_set_string_member(tool, "arguments", arguments);
    g_free(arguments);
    return tool;
}

static JsonObject *responses_object(JsonObject *action) {
    JsonObject *root = json_object_new();
    JsonArray *output = json_array_new();
    json_object_set_string_member(root, "id", "resp_fixture");
    json_object_set_string_member(root, "status", "completed");
    JsonObject *reasoning = json_object_new();
    json_object_set_string_member(reasoning, "type", "reasoning");
    json_object_set_string_member(reasoning, "id", "rs_fixture");
    json_object_set_string_member(reasoning, "encrypted_content", "opaque-reasoning-fixture");
    json_object_set_array_member(reasoning, "summary", json_array_new());
    json_array_add_object_element(output, reasoning);
    JsonObject *message = json_object_new(), *content = json_object_new();
    JsonArray *contents = json_array_new();
    json_object_set_string_member(message, "type", "message");
    json_object_set_string_member(message, "id", "msg_fixture");
    json_object_set_string_member(message, "role", "assistant");
    json_object_set_string_member(message, "phase", "commentary");
    json_object_set_string_member(message, "status", "completed");
    json_object_set_string_member(content, "type", "output_text");
    json_object_set_string_member(content, "text", "Inspecting the visible field.");
    json_object_set_array_member(content, "annotations", json_array_new());
    json_array_add_object_element(contents, content);
    json_object_set_array_member(message, "content", contents);
    json_array_add_object_element(output, message);
    json_array_add_object_element(output, tool_item(action, "call_fixture"));
    json_object_set_array_member(root, "output", output);
    return root;
}

static char *accept(PcAgent *a, JsonObject *action, PcAction *out, GError **e) {
    char *body;
    if(a->responses) {
        JsonObject *root = responses_object(action);
        body = encode_object(root); json_object_unref(root);
    } else body = chat_body(action);
    char *result = pc_agent_accept(a, body, out, e);
    g_free(body);
    return result;
}

static void init(PcAgent *a, gboolean responses) {
    GError *e = NULL;
    pc_agent_init(a, responses, responses ? "gpt-6-astra" : "local-vision", "Enter hello", "Use the test app");
    g_assert_true(pc_agent_observe(a, &frame, "Initial approved capture", &e));
    g_assert_no_error(e);
}

static void assert_rejected(PcAgent *a, JsonObject *action) {
    PcAction out = {0}; GError *e = NULL;
    guint history_length = json_array_get_length(a->history);
    g_assert_null(accept(a, action, &out, &e));
    g_assert_nonnull(e);
    g_assert_null(out.kind);
    g_assert_null(a->call_id);
    g_assert_cmpuint(json_array_get_length(a->history), ==, history_length);
    g_clear_error(&e); pc_action_clear(&out);
}

static void responses_request(void) {
    PcAgent a = {0}; GError *e = NULL;
    pc_agent_init(&a, TRUE, "gpt-6-astra", "Enter hello", "Use test app");
    g_assert_null(pc_agent_request(&a, &e)); g_assert_nonnull(e); g_clear_error(&e);
    g_assert_true(pc_agent_observe(&a, &frame, "Initial approved capture", &e));
    char *request = pc_agent_request(&a, &e); g_assert_no_error(e);
    JsonParser *p = parse(request); JsonObject *root = json_node_get_object(json_parser_get_root(p));
    g_assert_false(json_object_get_boolean_member(root, "store"));
    g_assert_false(json_object_get_boolean_member(root, "parallel_tool_calls"));
    g_assert_cmpstr(json_object_get_string_member(root, "model"), ==, "gpt-6-astra");
    g_assert_cmpstr(json_object_get_string_member(json_object_get_object_member(root, "reasoning"), "effort"), ==, "medium");
    g_assert_cmpstr(json_array_get_string_element(json_object_get_array_member(root, "include"), 0), ==, "reasoning.encrypted_content");
    JsonObject *tool = json_array_get_object_element(json_object_get_array_member(root, "tools"), 0);
    g_assert_true(json_object_get_boolean_member(tool, "strict"));
    g_assert_cmpstr(json_object_get_string_member(tool, "name"), ==, "desktop_action");
    JsonObject *parameters = json_object_get_object_member(tool, "parameters");
    g_assert_false(json_object_get_boolean_member(parameters, "additionalProperties"));
    JsonObject *properties = json_object_get_object_member(parameters, "properties");
    JsonArray *required = json_object_get_array_member(parameters, "required");
    g_assert_cmpuint(json_object_get_size(properties), ==, 12);
    g_assert_cmpuint(json_array_get_length(required), ==, 12);
    for(guint i = 0; i < json_array_get_length(required); i++)
        g_assert_true(json_object_has_member(properties, json_array_get_string_element(required, i)));
    JsonArray *input = json_object_get_array_member(root, "input");
    JsonArray *content = json_object_get_array_member(json_array_get_object_element(input, 0), "content");
    JsonObject *image = json_array_get_object_element(content, 1);
    g_assert_cmpstr(json_object_get_string_member(image, "type"), ==, "input_image");
    g_assert_cmpstr(json_object_get_string_member(image, "detail"), ==, "original");
    g_assert_cmpstr(json_object_get_string_member(image, "image_url"), ==, frame.image);
    g_assert_nonnull(strstr(json_object_get_string_member(json_array_get_object_element(content, 0), "text"), "800 x 600"));
    g_object_unref(p); g_free(request); pc_agent_clear(&a);
}

static void chat_request(void) {
    PcAgent a = {0}; PcAction out = {0}; GError *e = NULL; init(&a, FALSE);
    char *request = pc_agent_request(&a, &e); g_assert_no_error(e);
    JsonParser *p = parse(request); JsonObject *root = json_node_get_object(json_parser_get_root(p));
    g_assert_false(json_object_has_member(root, "tools"));
    g_assert_false(json_object_has_member(root, "input"));
    JsonArray *messages = json_object_get_array_member(root, "messages");
    g_assert_cmpuint(json_array_get_length(messages), ==, 2);
    g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(messages, 0), "role"), ==, "system");
    JsonArray *content = json_object_get_array_member(json_array_get_object_element(messages, 1), "content");
    JsonObject *image = json_array_get_object_element(content, 1);
    g_assert_cmpstr(json_object_get_string_member(image, "type"), ==, "image_url");
    g_assert_cmpstr(json_object_get_string_member(json_object_get_object_member(image, "image_url"), "url"), ==, frame.image);
    JsonObject *action = action_object(1, "click", "first", "ongoing");
    char *accepted = accept(&a, action, &out, &e); g_assert_no_error(e); g_assert_nonnull(accepted);
    g_assert_cmpstr(out.kind, ==, "click");
    g_assert_true(pc_agent_observe(&a, &frame, "Click injected; verify result", &e));
    g_assert_cmpuint(json_array_get_length(a.history), ==, 4);
    g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(a.history, 2), "role"), ==, "assistant");
    g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(a.history, 3), "role"), ==, "user");
    json_object_unref(action); g_free(accepted); g_free(request); g_object_unref(p); pc_action_clear(&out); pc_agent_clear(&a);
}

static void responses_history(void) {
    PcAgent a = {0}; PcAction out = {0}; GError *e = NULL; init(&a, TRUE);
    char *prefix = encode(json_array_get_element(a.history, 0));
    JsonObject *action = action_object(1, "click", "first", "ongoing");
    JsonObject *response = responses_object(action); char *body = encode_object(response);
    char *accepted = pc_agent_accept(&a, body, &out, &e); g_assert_no_error(e); g_assert_nonnull(accepted);
    g_assert_cmpstr(a.call_id, ==, "call_fixture");
    g_assert_null(pc_agent_request(&a, &e)); g_assert_nonnull(e); g_clear_error(&e);
    JsonArray *expected = json_object_get_array_member(response, "output");
    for(guint i = 0; i < json_array_get_length(expected); i++) {
        char *before = encode(json_array_get_element(expected, i));
        char *after = encode(json_array_get_element(a.history, i + 1));
        g_assert_cmpstr(after, ==, before); g_free(before); g_free(after);
    }
    PcSession next = frame; next.image = "data:image/png;base64,fixture-two";
    g_assert_true(pc_agent_observe(&a, &next, "Click delivered; visible outcome not yet verified", &e));
    g_assert_no_error(e); g_assert_null(a.call_id); g_assert_cmpuint(a.frame, ==, 2);
    char *after = encode(json_array_get_element(a.history, 0)); g_assert_cmpstr(after, ==, prefix);
    JsonObject *output = json_array_get_object_element(a.history, 4);
    g_assert_cmpstr(json_object_get_string_member(output, "type"), ==, "function_call_output");
    g_assert_cmpstr(json_object_get_string_member(output, "call_id"), ==, "call_fixture");
    JsonArray *observation = json_object_get_array_member(output, "output");
    JsonObject *image = json_array_get_object_element(observation, 1);
    g_assert_cmpstr(json_object_get_string_member(image, "image_url"), ==, next.image);
    g_assert_cmpstr(json_object_get_string_member(image, "detail"), ==, "original");
    char *request = pc_agent_request(&a, &e); g_assert_no_error(e);
    JsonParser *p = parse(request); JsonArray *replayed = json_object_get_array_member(json_node_get_object(json_parser_get_root(p)), "input");
    g_assert_cmpuint(json_array_get_length(replayed), ==, 5);
    g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(replayed, 1), "encrypted_content"), ==, "opaque-reasoning-fixture");
    g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(replayed, 2), "phase"), ==, "commentary");
    json_object_unref(action); action = action_object(2, "done", "verified", "completed");
    g_free(accepted); accepted = accept(&a, action, &out, &e);
    g_assert_no_error(e); g_assert_nonnull(accepted); g_assert_cmpstr(a.outcome, ==, "completed");
    json_object_unref(action); json_object_unref(response); g_object_unref(p);
    g_free(prefix); g_free(after); g_free(body); g_free(accepted); g_free(request); pc_action_clear(&out); pc_agent_clear(&a);
}

static void invalid_actions(void) {
    PcAgent a = {0}; pc_agent_init(&a, FALSE, "local-vision", "task", "answers");
    JsonObject *action = action_object(0, "click", "verified", "ongoing");
    assert_rejected(&a, action); json_object_unref(action); pc_agent_clear(&a);
    init(&a, FALSE);
    action = action_object(0, "click", "first", "ongoing");
    assert_rejected(&a, action); json_object_unref(action);
    action = action_object(1, "done", "first", "completed");
    json_object_remove_member(action, "text"); assert_rejected(&a, action); json_object_unref(action);
    action = action_object(1, "click", "first", "ongoing");
    json_object_set_string_member(action, "x", "80"); assert_rejected(&a, action); json_object_unref(action);
    action = action_object(1, "done", "first", "completed");
    json_object_set_double_member(action, "delta", 0.5); assert_rejected(&a, action); json_object_unref(action);
    action = action_object(1, "click", "first", "ongoing");
    json_object_set_string_member(action, "evidence", ""); assert_rejected(&a, action); json_object_unref(action);
    action = action_object(1, "click", "first", "ongoing");
    json_object_set_int_member(action, "frame", 2); assert_rejected(&a, action); json_object_unref(action);
    action = action_object(1, "click", "first", "ongoing");
    json_object_set_string_member(action, "extra", "unadvertised"); assert_rejected(&a, action); json_object_unref(action);
    pc_agent_clear(&a);
}

static void invalid_responses(void) {
    PcAgent a = {0}; PcAction out = {0}; GError *e = NULL; init(&a, TRUE);
    JsonObject *action = action_object(1, "click", "first", "ongoing");
    for(guint variant = 0; variant < 5; variant++) {
        JsonObject *root = responses_object(action); JsonArray *output = json_object_get_array_member(root, "output");
        if(variant == 0) json_object_set_string_member(root, "status", "incomplete");
        if(variant == 1) json_array_add_object_element(output, tool_item(action, "call_second"));
        if(variant == 2) json_object_set_string_member(json_array_get_object_element(output, 2), "name", "run_shell");
        if(variant == 3) json_array_remove_element(output, 2); /* commentary is not a final action */
        if(variant == 4) json_object_set_string_member(json_array_get_object_element(output, 2), "arguments", "{bad-json");
        char *body = encode_object(root); g_assert_null(pc_agent_accept(&a, body, &out, &e));
        g_assert_nonnull(e); g_assert_null(a.call_id); g_assert_null(out.kind);
        g_assert_cmpuint(json_array_get_length(a.history), ==, 1);
        g_clear_error(&e); g_free(body); json_object_unref(root);
    }
    json_object_unref(action); pc_action_clear(&out); pc_agent_clear(&a);
}

static void uncertain_completion(void) {
    PcAgent a = {0}; PcAction out = {0}; GError *e = NULL; init(&a, FALSE);
    JsonObject *action = action_object(1, "click", "first", "ongoing");
    char *accepted = accept(&a, action, &out, &e); g_assert_nonnull(accepted); g_assert_no_error(e);
    g_free(accepted); json_object_unref(action); pc_action_clear(&out);
    g_assert_true(pc_agent_observe(&a, &frame, "Input delivered; result uncertain", &e));
    action = action_object(2, "done", "uncertain", "completed"); assert_rejected(&a, action);
    json_object_set_string_member(action, "outcome", "blocked");
    accepted = accept(&a, action, &out, &e); g_assert_nonnull(accepted); g_assert_no_error(e);
    g_assert_cmpstr(a.outcome, ==, "blocked");
    g_free(accepted); json_object_unref(action); pc_action_clear(&out); pc_agent_clear(&a);
}

static void repeated_input(void) {
    PcAgent a = {0}; PcAction out = {0}; GError *e = NULL; init(&a, FALSE);
    JsonObject *action = action_object(1, "type", "first", "ongoing");
    char *accepted = accept(&a, action, &out, &e); g_assert_nonnull(accepted); g_assert_no_error(e);
    g_free(accepted); json_object_unref(action); pc_action_clear(&out);
    g_assert_true(pc_agent_observe(&a, &frame, "Input delivered; inspect resulting text", &e));
    action = action_object(2, "type", "uncertain", "ongoing"); assert_rejected(&a, action);
    json_object_unref(action); pc_agent_clear(&a);
    init(&a, TRUE);
    for(guint step = 1; step <= 4; step++) {
        action = action_object(step, "click", step == 1 ? "first" : "verified", "ongoing");
        if(step == 4) assert_rejected(&a, action);
        else {
            accepted = accept(&a, action, &out, &e); g_assert_nonnull(accepted); g_assert_no_error(e);
            g_free(accepted); pc_action_clear(&out);
            g_assert_true(pc_agent_observe(&a, &frame, "Input delivered; inspect result", &e));
        }
        json_object_unref(action);
    }
    pc_agent_clear(&a);
}

static void observation_limit(void) {
    PcAgent a = {0}; GError *e = NULL; init(&a, TRUE);
    guint count = json_array_get_length(a.history);
    a.bytes = 32u * 1024u * 1024u;
    g_assert_false(pc_agent_observe(&a, &frame, "Further capture", &e));
    g_assert_nonnull(e); g_assert_cmpuint(a.frame, ==, 1);
    g_assert_cmpuint(json_array_get_length(a.history), ==, count);
    g_clear_error(&e); pc_agent_clear(&a);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/pc-agent/responses-request", responses_request);
    g_test_add_func("/pc-agent/chat-request", chat_request);
    g_test_add_func("/pc-agent/append-only-history", responses_history);
    g_test_add_func("/pc-agent/invalid-actions", invalid_actions);
    g_test_add_func("/pc-agent/invalid-responses", invalid_responses);
    g_test_add_func("/pc-agent/uncertain-completion", uncertain_completion);
    g_test_add_func("/pc-agent/repeated-input", repeated_input);
    g_test_add_func("/pc-agent/observation-budget", observation_limit);
    return g_test_run();
}
