#include "pc_agent.h"
#include <gio/gio.h>
#include <string.h>

/* Protocol fixtures never capture a screen, inject input or contact a provider.
 * Approval/denial integration is exercised by the engine's separate smoke test. */
static const PcSession frame={
    .image="data:image/png;base64,fixture-one",.image_width=800,.image_height=600,
    .structured="{\"window\":{\"title\":\"Fixture\",\"process\":\"test.exe\"},\"focus\":{\"role\":\"edit\",\"name\":\"Message\",\"value\":\"\",\"bounds\":[10,20,200,30]},\"controls\":[],\"cursor\":[80,100],\"thin\":false}"
};
static char *encode(JsonNode *n){JsonGenerator *g=json_generator_new();json_generator_set_root(g,n);char *s=json_generator_to_data(g,NULL);g_object_unref(g);return s;}
static char *encode_object(JsonObject *o){JsonNode *n=json_node_new(JSON_NODE_OBJECT);json_node_set_object(n,o);char *s=encode(n);json_node_free(n);return s;}
static JsonParser *parse(const char *text){JsonParser *p=json_parser_new();GError *e=NULL;g_assert_true(json_parser_load_from_data(p,text,-1,&e));g_assert_no_error(e);g_assert_true(JSON_NODE_HOLDS_OBJECT(json_parser_get_root(p)));return p;}
static JsonObject *action(const char *kind){
    JsonObject *o=json_object_new();json_object_set_string_member(o,"action",kind);json_object_set_string_member(o,"reason","Operate the disposable test field");
    json_object_set_string_member(o,"text",!strcmp(kind,"type")?"hello":!strcmp(kind,"key")?"Enter":"");
    gboolean pointing=!strcmp(kind,"move")||!strcmp(kind,"click")||!strcmp(kind,"scroll");
    json_object_set_int_member(o,"x",pointing?80:0);json_object_set_int_member(o,"y",pointing?100:0);json_object_set_int_member(o,"delta",!strcmp(kind,"scroll")?1:0);
    json_object_set_string_member(o,"expected","The requested state becomes visible");return o;
}
static JsonObject *plan(guint frame_number,const char *kind,const char *verification,const char *outcome){
    JsonObject *o=json_object_new();json_object_set_int_member(o,"frame",frame_number);json_object_set_string_member(o,"verification",verification);
    json_object_set_string_member(o,"evidence","Structured state contains an editable Message field");json_object_set_string_member(o,"outcome",outcome);
    JsonArray *actions=json_array_new();if(kind)json_array_add_object_element(actions,action(kind));json_object_set_array_member(o,"actions",actions);return o;
}
static JsonObject *first_action(JsonObject *p){return json_array_get_object_element(json_object_get_array_member(p,"actions"),0);}
static char *chat_body(JsonObject *p){
    char *arguments=encode_object(p);JsonObject *root=json_object_new(),*choice=json_object_new(),*message=json_object_new();JsonArray *choices=json_array_new();
    json_object_set_string_member(message,"content",arguments);json_object_set_object_member(choice,"message",message);json_object_set_string_member(choice,"finish_reason","stop");
    json_array_add_object_element(choices,choice);json_object_set_array_member(root,"choices",choices);char *result=encode_object(root);json_object_unref(root);g_free(arguments);return result;
}
static JsonObject *tool_item(JsonObject *p,const char *call_id){
    JsonObject *tool=json_object_new();char *arguments=encode_object(p);json_object_set_string_member(tool,"type","function_call");json_object_set_string_member(tool,"id","fc_fixture");
    json_object_set_string_member(tool,"call_id",call_id);json_object_set_string_member(tool,"name","desktop_action");json_object_set_string_member(tool,"status","completed");json_object_set_string_member(tool,"arguments",arguments);g_free(arguments);return tool;
}
static JsonObject *responses_object(JsonObject *p){
    JsonObject *root=json_object_new();JsonArray *output=json_array_new();json_object_set_string_member(root,"id","resp_fixture");json_object_set_string_member(root,"status","completed");
    JsonObject *reasoning=json_object_new();json_object_set_string_member(reasoning,"type","reasoning");json_object_set_string_member(reasoning,"id","rs_fixture");json_object_set_string_member(reasoning,"encrypted_content","opaque-reasoning-fixture");json_object_set_array_member(reasoning,"summary",json_array_new());json_array_add_object_element(output,reasoning);
    JsonObject *message=json_object_new(),*text=json_object_new();JsonArray *content=json_array_new();json_object_set_string_member(message,"type","message");json_object_set_string_member(message,"id","msg_fixture");json_object_set_string_member(message,"role","assistant");json_object_set_string_member(message,"phase","commentary");json_object_set_string_member(message,"status","completed");
    json_object_set_string_member(text,"type","output_text");json_object_set_string_member(text,"text","Inspecting the field.");json_object_set_array_member(text,"annotations",json_array_new());json_array_add_object_element(content,text);json_object_set_array_member(message,"content",content);json_array_add_object_element(output,message);
    json_array_add_object_element(output,tool_item(p,"call_fixture"));json_object_set_array_member(root,"output",output);return root;
}
static char *accept(PcAgent *a,JsonObject *p,PcPlan *out,GError **e){char *body;if(a->responses){JsonObject *root=responses_object(p);body=encode_object(root);json_object_unref(root);}else body=chat_body(p);char *result=pc_agent_accept_plan(a,body,out,e);g_free(body);return result;}
static void init(PcAgent *a,gboolean responses){GError *e=NULL;pc_agent_init(a,responses,responses?"gpt-6-astra":"local-text-7b","Enter hello","Use the test app");g_assert_true(pc_agent_observe(a,&frame,"Initial structured observation",FALSE,&e));g_assert_no_error(e);}
static JsonArray *last_content(PcAgent *a){JsonObject *o=json_array_get_object_element(a->history,json_array_get_length(a->history)-1);return json_object_get_array_member(o,json_object_has_member(o,"output")?"output":"content");}
static JsonParser *last_payload(PcAgent *a){return parse(json_object_get_string_member(json_array_get_object_element(last_content(a),0),"text"));}
static void assert_rejected(PcAgent *a,JsonObject *p){
    PcPlan out={0};GError *e=NULL;guint count=json_array_get_length(a->history);gsize bytes=a->bytes;char *desktop=a->desktop?encode(a->desktop):NULL;
    g_assert_null(accept(a,p,&out,&e));g_assert_nonnull(e);g_assert_cmpuint(out.count,==,0);g_assert_null(out.actions[0].kind);g_assert_null(a->call_id);g_assert_false(a->awaiting_observation);
    g_assert_cmpuint(json_array_get_length(a->history),==,count);g_assert_cmpuint(a->bytes,==,bytes);char *after=a->desktop?encode(a->desktop):NULL;g_assert_cmpstr(after,==,desktop);
    g_free(desktop);g_free(after);g_clear_error(&e);pc_plan_clear(&out);
}

static void structured_default(void){
    for(int responses=0;responses<=1;responses++){
        PcAgent a={0};GError *e=NULL;init(&a,responses);g_assert_cmpuint(json_array_get_length(last_content(&a)),==,1);
        char *request=pc_agent_request(&a,&e);g_assert_no_error(e);g_assert_null(strstr(request,"data:image"));g_assert_null(strstr(request,"\"input_image\""));g_assert_null(strstr(request,"\"image_url\""));
        JsonParser *payload=last_payload(&a);JsonObject *o=json_node_get_object(json_parser_get_root(payload));g_assert_true(json_object_has_member(o,"desktop"));g_assert_false(json_object_get_boolean_member(o,"image_attached"));g_assert_cmpint(json_object_get_int_member(o,"frame"),==,1);
        PcSession no_image=frame;no_image.image=NULL;g_assert_true(pc_agent_observe(&a,&no_image,"Structured state alone is sufficient",FALSE,&e));g_assert_no_error(e);
        g_object_unref(payload);g_free(request);pc_agent_clear(&a);
    }
}
static void optional_image(void){
    for(int responses=0;responses<=1;responses++){
        PcAgent a={0};GError *e=NULL;init(&a,responses);PcSession thin=frame;thin.structured_thin=TRUE;
        g_assert_true(pc_agent_observe(&a,&thin,"Thin state awaits screenshot approval",FALSE,&e));g_assert_cmpuint(json_array_get_length(last_content(&a)),==,1);
        g_assert_true(pc_agent_observe(&a,&thin,"Screenshot explicitly approved",TRUE,&e));g_assert_no_error(e);JsonArray *content=last_content(&a);g_assert_cmpuint(json_array_get_length(content),==,2);
        JsonObject *image=json_array_get_object_element(content,1);g_assert_cmpstr(json_object_get_string_member(image,"type"),==,responses?"input_image":"image_url");
        const char *url=responses?json_object_get_string_member(image,"image_url"):json_object_get_string_member(json_object_get_object_member(image,"image_url"),"url");g_assert_cmpstr(url,==,frame.image);
        guint count=json_array_get_length(a.history),number=a.frame;thin.image=NULL;g_assert_false(pc_agent_observe(&a,&thin,"Missing approved capture",TRUE,&e));g_assert_nonnull(e);g_assert_cmpuint(a.frame,==,number);g_assert_cmpuint(json_array_get_length(a.history),==,count);g_clear_error(&e);pc_agent_clear(&a);
    }
}
static void strict_schema(void){
    PcAgent a={0};GError *e=NULL;init(&a,TRUE);char *request=pc_agent_request(&a,&e);g_assert_no_error(e);JsonParser *p=parse(request);JsonObject *root=json_node_get_object(json_parser_get_root(p));
    g_assert_false(json_object_get_boolean_member(root,"store"));g_assert_false(json_object_get_boolean_member(root,"parallel_tool_calls"));g_assert_cmpstr(json_object_get_string_member(root,"model"),==,"gpt-6-astra");
    g_assert_cmpstr(json_array_get_string_element(json_object_get_array_member(root,"include"),0),==,"reasoning.encrypted_content");JsonObject *tool=json_array_get_object_element(json_object_get_array_member(root,"tools"),0);g_assert_true(json_object_get_boolean_member(tool,"strict"));
    JsonObject *schema=json_object_get_object_member(tool,"parameters");g_assert_false(json_object_get_boolean_member(schema,"additionalProperties"));g_assert_cmpuint(json_array_get_length(json_object_get_array_member(schema,"required")),==,5);
    JsonObject *actions=json_object_get_object_member(json_object_get_object_member(schema,"properties"),"actions");g_assert_cmpint(json_object_get_int_member(actions,"maxItems"),==,3);JsonObject *item=json_object_get_object_member(actions,"items");g_assert_false(json_object_get_boolean_member(item,"additionalProperties"));g_assert_cmpuint(json_array_get_length(json_object_get_array_member(item,"required")),==,7);
    g_object_unref(p);g_free(request);pc_agent_clear(&a);
}
static void local_bounded_state(void){
    PcAgent a={0};PcPlan out={0};GError *e=NULL;init(&a,FALSE);
    g_assert_true(pc_agent_observe(&a,&frame,"Approved image once",TRUE,&e));
    for(guint i=2;i<10;i++){
        JsonObject *p=plan(i,"click","verified","ongoing");json_object_set_int_member(first_action(p),"x",(gint64)i+40);char *accepted=accept(&a,p,&out,&e);g_assert_nonnull(accepted);g_assert_no_error(e);g_assert_null(pc_agent_request(&a,&e));g_assert_nonnull(e);g_clear_error(&e);
        g_assert_true(pc_agent_observe(&a,&frame,"Click delivered; verify current state",FALSE,&e));g_assert_no_error(e);g_assert_cmpuint(json_array_get_length(a.history),==,4);
        char *request=pc_agent_request(&a,&e);g_assert_no_error(e);g_assert_null(strstr(request,"data:image"));JsonParser *q=parse(request);JsonObject *root=json_node_get_object(json_parser_get_root(q));g_assert_false(json_object_has_member(root,"input"));g_assert_false(json_object_has_member(root,"tools"));g_assert_cmpstr(json_object_get_string_member(root,"model"),==,"local-text-7b");
        g_assert_nonnull(strstr(request,"Enter hello"));g_assert_cmpuint(strlen(request),<,8000);JsonParser *payload=last_payload(&a);g_assert_true(json_object_has_member(json_node_get_object(json_parser_get_root(payload)),"desktop"));g_object_unref(payload);g_object_unref(q);g_free(request);g_free(accepted);json_object_unref(p);pc_plan_clear(&out);
    }
    pc_agent_clear(&a);
}
static void responses_deltas(void){
    PcAgent a={0};PcPlan out={0};GError *e=NULL;init(&a,TRUE);char *prefix=encode(json_array_get_element(a.history,0));JsonObject *p=plan(1,"click","first","ongoing"),*response=responses_object(p);char *body=encode_object(response),*accepted=pc_agent_accept_plan(&a,body,&out,&e);g_assert_nonnull(accepted);g_assert_no_error(e);
    g_assert_cmpstr(a.call_id,==,"call_fixture");g_assert_null(pc_agent_request(&a,&e));g_assert_nonnull(e);g_clear_error(&e);JsonArray *output=json_object_get_array_member(response,"output");
    for(guint i=0;i<json_array_get_length(output);i++){char *before=encode(json_array_get_element(output,i)),*after=encode(json_array_get_element(a.history,i+1));g_assert_cmpstr(after,==,before);g_free(before);g_free(after);}
    PcSession next=frame;next.structured="{\"window\":{\"title\":\"Fixture\",\"process\":\"test.exe\"},\"focus\":{\"role\":\"edit\",\"name\":\"Message\",\"value\":\"hello\",\"bounds\":[10,20,200,30]},\"controls\":[],\"cursor\":[80,100],\"thin\":false}";
    g_assert_true(pc_agent_observe(&a,&next,"Click delivered; field focused",FALSE,&e));g_assert_no_error(e);g_assert_null(a.call_id);char *after=encode(json_array_get_element(a.history,0));g_assert_cmpstr(after,==,prefix);
    JsonObject *tool_output=json_array_get_object_element(a.history,4);g_assert_cmpstr(json_object_get_string_member(tool_output,"type"),==,"function_call_output");g_assert_cmpstr(json_object_get_string_member(tool_output,"call_id"),==,"call_fixture");g_assert_cmpuint(json_array_get_length(last_content(&a)),==,1);
    JsonParser *payload=last_payload(&a);JsonObject *data=json_node_get_object(json_parser_get_root(payload));g_assert_false(json_object_has_member(data,"task"));g_assert_false(json_object_has_member(data,"desktop"));g_assert_cmpint(json_object_get_int_member(data,"base_frame"),==,1);JsonObject *delta=json_object_get_object_member(data,"desktop_delta");g_assert_cmpuint(json_object_get_size(delta),==,1);g_assert_true(json_object_has_member(delta,"focus"));
    char *request=pc_agent_request(&a,&e);g_assert_no_error(e);g_assert_nonnull(strstr(request,"opaque-reasoning-fixture"));g_assert_nonnull(strstr(request,"commentary"));
    g_free(prefix);g_free(after);g_free(body);g_free(accepted);g_free(request);g_object_unref(payload);json_object_unref(response);json_object_unref(p);pc_plan_clear(&out);pc_agent_clear(&a);
}
static void valid_batch(void){
    for(int responses=0;responses<=1;responses++){
        PcAgent a={0};PcPlan out={0};GError *e=NULL;init(&a,responses);JsonObject *p=plan(1,"move","first","ongoing");JsonArray *items=json_object_get_array_member(p,"actions");json_array_add_object_element(items,action("click"));json_array_add_object_element(items,action("type"));char *accepted=accept(&a,p,&out,&e);g_assert_no_error(e);g_assert_nonnull(accepted);g_assert_cmpuint(out.count,==,3);
        for(guint i=0;i<out.count;i++){char *approval=pc_plan_action_json(&out,i);PcAction parsed={0};g_assert_true(pc_parse(approval,&parsed,&e));g_assert_cmpstr(parsed.kind,==,out.actions[i].kind);g_assert_nonnull(strstr(approval,"expected"));pc_action_clear(&parsed);g_free(approval);}
        g_assert_null(pc_plan_action_json(&out,3));g_free(accepted);json_object_unref(p);pc_plan_clear(&out);pc_agent_clear(&a);
    }
}
static void invalid_plans(void){
    PcAgent a={0};pc_agent_init(&a,FALSE,"local-text-7b","task","");JsonObject *p=plan(0,"click","first","ongoing");assert_rejected(&a,p);json_object_unref(p);pc_agent_clear(&a);init(&a,FALSE);
    for(guint variant=0;variant<14;variant++){
        p=plan(1,"click","first","ongoing");JsonObject *item=first_action(p);
        switch(variant){
            case 0:json_object_set_int_member(p,"frame",0);break;
            case 1:json_object_remove_member(item,"text");break;
            case 2:json_object_set_string_member(item,"x","80");break;
            case 3:json_object_set_double_member(item,"delta",0.5);break;
            case 4:json_object_set_string_member(p,"evidence","");break;
            case 5:json_object_set_string_member(item,"extra","unadvertised");break;
            case 6:json_object_set_string_member(p,"extra","unadvertised");break;
            case 7:json_object_set_null_member(p,"actions");break;
            case 8:json_object_set_int_member(item,"delta",1);break;
            case 9:json_object_set_string_member(item,"text","unused");break;
            case 10:json_object_set_string_member(item,"action","done");break;
            case 11:json_object_set_string_member(p,"outcome","completed");break;
            case 12:json_object_set_string_member(p,"verification","verified");break;
            case 13:json_object_set_int_member(item,"x",1600);break;
        }
        assert_rejected(&a,p);json_object_unref(p);
    }
    pc_agent_clear(&a);
}
static void atomic_batch_rejection(void){
    for(int responses=0;responses<=1;responses++){
        PcAgent a={0};init(&a,responses);
        for(guint variant=0;variant<9;variant++){
            JsonObject *p=plan(1,"click","first","ongoing"),*second=action("type");JsonArray *items=json_object_get_array_member(p,"actions");json_array_add_object_element(items,second);
            switch(variant){
                case 0:json_object_set_string_member(second,"action","unknown");break;
                case 1:json_object_set_string_member(second,"text","submit\n");break;
                case 2:json_object_set_string_member(second,"text","submit\r");break;
                case 3:json_object_set_string_member(second,"action","key");json_object_set_string_member(second,"text","Enter");break;
                case 4:json_object_set_string_member(second,"action","click");json_object_set_string_member(second,"text","");break;
                case 5:json_array_add_object_element(items,action("screenshot"));break;
                case 6:json_array_add_object_element(items,action("move"));json_array_add_object_element(items,action("type"));break;
                case 7:json_object_set_string_member(first_action(p),"action","scroll");json_object_set_int_member(first_action(p),"delta",1);break;
                case 8:json_object_set_string_member(first_action(p),"action","move");json_object_set_string_member(second,"action","click");json_object_set_string_member(second,"text","");break;
            }
            assert_rejected(&a,p);json_object_unref(p);
        }
        /* A caller-owned previous proposal must also survive failed parsing. */
        PcPlan sentinel={.count=1};sentinel.actions[0].kind=g_strdup("sentinel");GError *e=NULL;g_assert_null(pc_agent_accept_plan(&a,"{broken",&sentinel,&e));g_assert_nonnull(e);g_assert_cmpstr(sentinel.actions[0].kind,==,"sentinel");g_clear_error(&e);pc_plan_clear(&sentinel);pc_agent_clear(&a);
    }
}
static void invalid_responses(void){
    PcAgent a={0};PcPlan out={0};GError *e=NULL;init(&a,TRUE);JsonObject *p=plan(1,"click","first","ongoing");
    for(guint variant=0;variant<6;variant++){
        JsonObject *root=responses_object(p);JsonArray *output=json_object_get_array_member(root,"output");
        if(variant==0)json_object_set_string_member(root,"status","incomplete");
        if(variant==1)json_array_add_object_element(output,tool_item(p,"call_second"));
        if(variant==2)json_object_set_string_member(json_array_get_object_element(output,2),"name","run_shell");
        if(variant==3)json_array_remove_element(output,2);
        if(variant==4)json_object_set_string_member(json_array_get_object_element(output,2),"arguments","{bad-json");
        if(variant==5)json_object_set_string_member(json_array_get_object_element(output,2),"status","incomplete");
        char *body=encode_object(root);g_assert_null(pc_agent_accept_plan(&a,body,&out,&e));g_assert_nonnull(e);g_assert_null(a.call_id);g_assert_cmpuint(out.count,==,0);g_assert_cmpuint(json_array_get_length(a.history),==,1);g_clear_error(&e);g_free(body);json_object_unref(root);
    }
    json_object_unref(p);pc_plan_clear(&out);pc_agent_clear(&a);
}
static void truncated_local(void){
    PcAgent a={0};PcPlan out={0};GError *e=NULL;init(&a,FALSE);JsonObject *p=plan(1,"click","first","ongoing");char *complete=chat_body(p);JsonParser *parser=parse(complete);JsonObject *root=json_node_get_object(json_parser_get_root(parser)),*choice=json_array_get_object_element(json_object_get_array_member(root,"choices"),0);json_object_set_string_member(choice,"finish_reason","length");char *truncated=encode_object(root);g_assert_null(pc_agent_accept_plan(&a,truncated,&out,&e));g_assert_nonnull(e);g_assert_cmpuint(out.count,==,0);g_clear_error(&e);
    json_object_set_string_member(choice,"finish_reason","stop");json_object_set_string_member(json_object_get_object_member(choice,"message"),"content","{\"frame\":1,\"actions\":[{\"action\":\"click\"}");g_free(truncated);truncated=encode_object(root);g_assert_null(pc_agent_accept_plan(&a,truncated,&out,&e));g_assert_nonnull(e);g_assert_cmpuint(out.count,==,0);g_assert_false(a.responses);g_assert_cmpstr(a.model,==,"local-text-7b");g_clear_error(&e);
    g_free(complete);g_free(truncated);json_object_unref(p);g_object_unref(parser);pc_plan_clear(&out);pc_agent_clear(&a);
}
static void uncertain_completion(void){
    PcAgent a={0};PcPlan out={0};GError *e=NULL;init(&a,FALSE);JsonObject *p=plan(1,"click","first","ongoing");char *accepted=accept(&a,p,&out,&e);g_assert_nonnull(accepted);g_assert_no_error(e);g_free(accepted);json_object_unref(p);pc_plan_clear(&out);
    g_assert_true(pc_agent_observe(&a,&frame,"Input delivered; result uncertain",FALSE,&e));p=plan(2,NULL,"uncertain","completed");assert_rejected(&a,p);json_object_set_string_member(p,"outcome","blocked");accepted=accept(&a,p,&out,&e);g_assert_nonnull(accepted);g_assert_no_error(e);g_assert_cmpstr(a.outcome,==,"blocked");g_assert_cmpuint(out.count,==,0);
    g_free(accepted);json_object_unref(p);pc_plan_clear(&out);pc_agent_clear(&a);
}
static void repeated_input(void){
    PcAgent a={0};PcPlan out={0};GError *e=NULL;init(&a,FALSE);JsonObject *p=plan(1,"type","first","ongoing");char *accepted=accept(&a,p,&out,&e);g_assert_nonnull(accepted);g_assert_no_error(e);g_free(accepted);json_object_unref(p);pc_plan_clear(&out);
    g_assert_true(pc_agent_observe(&a,&frame,"Input delivered; inspect resulting text",FALSE,&e));p=plan(2,"type","uncertain","ongoing");assert_rejected(&a,p);json_object_unref(p);pc_agent_clear(&a);init(&a,TRUE);
    for(guint step=1;step<=4;step++){
        p=plan(step,"click",step==1?"first":"verified","ongoing");if(step==4)assert_rejected(&a,p);else{accepted=accept(&a,p,&out,&e);g_assert_nonnull(accepted);g_assert_no_error(e);g_free(accepted);pc_plan_clear(&out);g_assert_true(pc_agent_observe(&a,&frame,"Input delivered; inspect result",FALSE,&e));}json_object_unref(p);
    }pc_agent_clear(&a);
}
static void observation_limits(void){
    PcAgent a={0};GError *e=NULL;init(&a,TRUE);guint count=json_array_get_length(a.history);a.bytes=32u*1024u*1024u;g_assert_false(pc_agent_observe(&a,&frame,"Further state",FALSE,&e));g_assert_nonnull(e);g_assert_cmpuint(a.frame,==,1);g_assert_cmpuint(json_array_get_length(a.history),==,count);g_clear_error(&e);pc_agent_clear(&a);init(&a,FALSE);
    PcSession bad=frame;bad.structured="[]";g_assert_false(pc_agent_observe(&a,&bad,"Malformed structured state",FALSE,&e));g_assert_nonnull(e);g_assert_cmpuint(a.frame,==,1);g_clear_error(&e);pc_agent_clear(&a);
}
int main(int argc,char **argv){
    g_test_init(&argc,&argv,NULL);
    g_test_add_func("/pc-agent/structured-default",structured_default);g_test_add_func("/pc-agent/optional-image",optional_image);g_test_add_func("/pc-agent/strict-schema",strict_schema);
    g_test_add_func("/pc-agent/local-bounded-state",local_bounded_state);g_test_add_func("/pc-agent/responses-deltas",responses_deltas);g_test_add_func("/pc-agent/valid-batch",valid_batch);
    g_test_add_func("/pc-agent/invalid-plans",invalid_plans);g_test_add_func("/pc-agent/atomic-batch-rejection",atomic_batch_rejection);g_test_add_func("/pc-agent/invalid-responses",invalid_responses);
    g_test_add_func("/pc-agent/truncated-local",truncated_local);g_test_add_func("/pc-agent/uncertain-completion",uncertain_completion);g_test_add_func("/pc-agent/repeated-input",repeated_input);g_test_add_func("/pc-agent/observation-limits",observation_limits);
    return g_test_run();
}
