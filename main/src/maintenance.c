#include "maintenance.h"

// static void parse_new_nodes(const char *content);
// static int gather_nodes(char *out_buffer);
static void update_name(ID origin_node, char buffer[32]);
/*

PING
    - request node to ping back
PING RESPONSE
    - send back node name

DISCOVERY 
    - request for neighbor nodes node table
DISCOVERY RESPONSE
    - return back a list of all nodes in node table

GLOBAL BCAST
    - broadcast but recursive to all nodes until hitting a node who already has that messages

GLOBAL BCAST RESPONSE
    - nothing, the acks themselfs will be enough information

*/

void handle_maintenance_msg(ID msg_id) {
    DataEntry *respond_to_msg = msg_find(msg_id);
    printf("MAINTENANCE msg handling for ID=%d : \"%s\"\n", respond_to_msg->id, respond_to_msg->content);
    // make sure msg is either broadcasted, or meant for this node
    // also check to make sure message has not already been received ******* DO THIS LATER

    if ((respond_to_msg->dst_node != 0) && (respond_to_msg->dst_node != g_address.i_addr)) {
        printf("msg %d not for this node\n",respond_to_msg->id);
        return;
    }

    // buffer for message to send back
    char buffer[240];
    int len = 0;

    // PING

    if (strncmp(respond_to_msg->content, "ping", 4) == 0) {
        NodeEntry *this_node = get_node_ptr(g_address.i_addr);
        // get node info
        // return back name of node
        len = sprintf(buffer,"%s",(this_node->name[0] != '\0') ? this_node->name : "None");

        // send ping msg back to the ORIGIN node, going to  the SRC node, as an ack for that msg
 
    } else if (respond_to_msg->ack_for) {
        DataEntry *acked_msg = msg_find(respond_to_msg->ack_for);
        if (strncmp(acked_msg->content, "ping", 4) == 0) {
            update_name(respond_to_msg->origin_node, respond_to_msg->content);
        }
    }

    // // DISCOVERY

    // // branch based on what kinda maintenace
    // if (strncmp(respond_to_msg->content, "discovery", 9) == 0) {
    //     len = gather_nodes(buffer);
    //     printf("gathered nodes (%d) \"%s\"\n",len,buffer);
    //     // recivce discovery command

    // } else if (respond_to_msg->ack_for) {
    //     // if msg is resposne to a discovery node
    //     DataEntry *acked_msg = msg_find(respond_to_msg->ack_for);
    //     // if it is the discovery message then deal with it
    //     if (strncmp(acked_msg->content, "discovery", 9) == 0) {
    //         parse_new_nodes(respond_to_msg->content);
    //     }
    // }

    // GLOBAL BCAST

    if (strncmp(respond_to_msg->content, "gbcast", 7) == 0) {
        // we need to create an ack to (origin, targeting src)
        // we also need to send along the msg
        NodeEntry *this_node = get_node_ptr(g_address.i_addr);

        // the ack should just have the nodes name
        for (; this_node->name[len]; len++) {
            buffer[len] = this_node->name[len];
        }
        buffer[len] = '\0';

        // now we need to broadcast the msg again
        queue_send(msg_id, BROADCAST_ID);


    } else if (respond_to_msg->ack_for) {
        // if msg is resposne to a discovery node
        DataEntry *acked_msg = msg_find(respond_to_msg->ack_for);
        // if it is the discovery message then deal with it
        if (strncmp(acked_msg->content, "gbcast", 7) == 0) {
            update_name(respond_to_msg->origin_node, respond_to_msg->content);
        }

    }


    
    if (len) {
        // only send a response message using buffer IF len is not 0
        printf("MAINTENANCE ack for = %d\n",msg_id);
        ID response_msg = create_data_object(NO_ID, MAINTENANCE, buffer, g_address.i_addr, respond_to_msg->origin_node, g_address.i_addr, 0, 0, 0, msg_id);
        queue_send(response_msg, respond_to_msg->src_node);
    }
}

static void update_name(ID origin_node, char buffer[32]) {
    NodeEntry *heard_node = get_node_ptr(origin_node);
    int c = 0;
    for (; c < (int)sizeof(heard_node->name) - 1 && buffer[c]; c++) {
        heard_node->name[c] = buffer[c];
    }
    heard_node->name[c] = '\0';
    printf("Node id = %s new name is: %s\n",heard_node->address.s_addr, heard_node->name);
}


// static int gather_nodes(char *out_buffer) {
//     if (!out_buffer) {
//         printf("No buffer given\n");
//         return 0;
//     }
//     int count = 0;
//     char node_list_buffer[256] = { 0 };
//     int len = 0;
//     NodeEntry *walk = g_node_table;

//     while (walk) {
//         len = strlcat(node_list_buffer, walk->address.s_addr, 256);
//         node_list_buffer[len++] = ':';
//         node_list_buffer[len] = '\0';
//         count++;
//         walk = walk->next;
//     }
//     if (count == 0) {
//         printf("No Nodes\n");
//         return 0;
//     }
//     node_list_buffer[--len] = '\0'; // remove last ','

//     return sprintf(out_buffer, "%d:%s", count, node_list_buffer);
// }


// static void parse_new_nodes(const char *content) {
//     if (!content) return;

//     const char *p = content;
//     char *end = NULL;

//     // 1) Parse count
//     long count = strtol(p, &end, 10);
//     if (end == p || *end != ':') {
//         printf("Bad node list header: \"%s\"\n", content);
//         return;
//     }

//     printf("%ld nodes found\n", count);

//     p = end + 1; // move past ':'

//     // 2) Parse each ID
//     for (long i = 0; i < count && *p; i++) {
//         long id_val = strtol(p, &end, 10);
//         if (end == p) {
//             printf("Failed to parse node %ld in \"%s\"\n", i, content);
//             break;
//         }

//         ID id = (ID)id_val;
//         printf("Node %ld: %hd\n", i, id);
//         NodeEntry *created = node_create_if_needed(id);
//         if (created) {
//             attempt_to_reach_node(id);
//         }
//         p = end;

//         if (*p == ':') {
//             p++;
//         }
//     }
// }

void resolve_system_command(char *cmd_buffer) {
    printf("System command: %s\n",cmd_buffer);

    char name[32];
    if (sscanf(cmd_buffer, "SYS+NAME=%31[^\r\n]",name)) {
        name[31] = '\0';
        int len = strlen(name);
        printf("New name is %s\n",name);
        NodeEntry *node = get_node_ptr(g_address.i_addr);
        strlcpy(node->name, name, 32);
        node->name[len] = '\0';
    }
}
