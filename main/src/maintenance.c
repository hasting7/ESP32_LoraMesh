#include "maintenance.h"

#include "routing.h"
#include "data_table.h"
#include "node_table.h"
#include "lora_uart.h"

#include <string.h>
#include <time.h>

#define RQUERY_INTERVAL_MS (120000)

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

    if ((respond_to_msg->dst_node != 0) && (respond_to_msg->dst_node != g_my_address)) {
        printf("msg %d not for this node\n",respond_to_msg->id);
        return;
    }
    bool should_ack_use_router = true;
    // buffer for message to send back
    char buffer[240];
    int len = 0;

    // PING

    if (strncmp(respond_to_msg->content, "ping", 5) == 0) {
        // get node info
        // return back name of node
        len = sprintf(buffer,"%s",(g_this_node->name[0] != '\0') ? g_this_node->name : "None");

        // send ping msg back to the ORIGIN node, going to  the SRC node, as an ack for that msg
 
    } else if (respond_to_msg->ack_for != NO_ID) {
        DataEntry *acked_msg = msg_find(respond_to_msg->ack_for);
        if (strncmp(acked_msg->content, "ping", 5) == 0) {
            update_name(respond_to_msg->origin_node, respond_to_msg->content);
        }
    }

    // GLOBAL BCAST

    if (strncmp(respond_to_msg->content, "gbcast", 7) == 0) {
        // we need to create an ack to (origin, targeting src)
        // we also need to send along the msg

        // the ack should just have the nodes name
        for (; g_this_node->name[len]; len++) {
            buffer[len] = g_this_node->name[len];
        }
        buffer[len] = '\0';

        // now we need to broadcast the msg again
        queue_send(msg_id, BROADCAST_ID, false);


    } else if (respond_to_msg->ack_for != NO_ID) {
        // if msg is resposne to a discovery node
        DataEntry *acked_msg = msg_find(respond_to_msg->ack_for);
        // if it is the discovery message then deal with it
        if (strncmp(acked_msg->content, "gbcast", 7) == 0) {
            update_name(respond_to_msg->origin_node, respond_to_msg->content);
        }

    }

    // RQUERY

    if (strncmp(respond_to_msg->content, "rquery", 7) == 0) { 
        NodeEntry *from_node = get_node_ptr(respond_to_msg->src_node);
        // this nodes router and the node obj of the src
        len = router_answer_rquery(g_this_node->router, from_node, 5, buffer, 240);
        // provide router details

    } else if (respond_to_msg->ack_for != NO_ID) {
        // if msg is resposne to a discovery node
        DataEntry *acked_msg = msg_find(respond_to_msg->ack_for);
        // if it is the discovery message then deal with it
        if (strncmp(acked_msg->content, "rquery", 7) == 0) {
            router_parse_rquery(g_this_node->router, respond_to_msg->src_node, respond_to_msg->content);
        }
    }

    // UNLINK

    if (strncmp(respond_to_msg->content, "unlink", 7) == 0) { 
        // do whats needed to unlink
        // this should only be allowed if they are direct neighbors (steps = 1)
        if (respond_to_msg->steps != 1) {
            // no we should not allow for unlinks when nodes are more than 1 step appart
            buffer[len++] = 'n';
        } else {
            should_ack_use_router = false;
            NodeEntry *linked_node = get_node_ptr(respond_to_msg->origin_node);
            // yes we should cut the link
            // tell router we are unlinking
            router_unlink_node(g_this_node->router, respond_to_msg->origin_node);
            // tell node the same
            linked_node->link_enabled = false;
            buffer[len++] = 'y';
        }
        buffer[len] = '\0';

    } else if (respond_to_msg->ack_for != NO_ID) {
        // if msg is resposne to a discovery node
        DataEntry *acked_msg = msg_find(respond_to_msg->ack_for);
        // if it is the discovery message then deal with it
        if (strncmp(acked_msg->content, "unlink", 7) == 0) {
            if (respond_to_msg->content[0] == 'y') {
                should_ack_use_router = false; // probably not needed but whatever
                // other node unlinked so we can unlink
                NodeEntry *linked_node = get_node_ptr(respond_to_msg->origin_node);
                // tell router we are unlinking
                router_unlink_node(g_this_node->router, respond_to_msg->origin_node);
                // tell node the same
                linked_node->link_enabled = false;
            }
        }
    }

    // LINK

    if (strncmp(respond_to_msg->content, "link", 5) == 0) { 
        // for link i should relink no questions asked

        NodeEntry *unlinked_node = get_node_ptr(respond_to_msg->origin_node);
        // yes we should establish the link
        // tell router we are linking
        router_link_node(g_this_node->router, respond_to_msg->origin_node);
        // tell node the same
        unlinked_node->link_enabled = true;

    }
    // no ack for link


    
    if (len) {
        // only send a response message using buffer IF len is not 0
        printf("MAINTENANCE ack for = %d\n",msg_id);
        ID response_msg = create_data_object(NO_ID, MAINTENANCE, buffer, g_my_address, respond_to_msg->origin_node, g_my_address, 0, 0, 0, msg_id);
        queue_send(response_msg, respond_to_msg->src_node, should_ack_use_router);
    }
}

static void update_name(ID origin_node, char buffer[32]) {
    NodeEntry *heard_node = get_node_ptr(origin_node);
    int c = 0;
    for (; c < (int)sizeof(heard_node->name) - 1 && buffer[c]; c++) {
        heard_node->name[c] = buffer[c];
    }
    heard_node->name[c] = '\0';
    printf("Node id = %hu new name is: %s\n",heard_node->address, heard_node->name);
}


void resolve_system_command(char *cmd_buffer) {
    printf("System command: %s\n",cmd_buffer);

    char name[32];
    ID node_id;
    if (sscanf(cmd_buffer, "SYS+NAME=%31[^\r\n]",name)) {
        name[31] = '\0';
        int len = strlen(name);
        printf("New name is %s\n",name);
        strlcpy(g_this_node->name, name, 32);
        g_this_node->name[len] = '\0';
    } else if (sscanf(cmd_buffer, "SYS+LINK=%hu",&node_id)) {
        if (node_id == g_my_address) {
            printf("[LINK] Cannot link to self\n");
            return;
        }
        NodeEntry *unlinked_node = get_node_ptr(node_id);
        if (!unlinked_node) {
            printf("[LINK] Node with id = %hu not found\n",node_id);
            return;
        }

        router_link_node(g_this_node->router, node_id);
        unlinked_node->link_enabled = true;

        // send msg of re link to neighbor
        ID unlink_msg = create_data_object(NO_ID, MAINTENANCE, "link", g_my_address, unlinked_node->address, g_my_address, 0, 0, 0, NO_ID);
        queue_send(unlink_msg, unlinked_node->address, false);

    } else if (sscanf(cmd_buffer, "SYS+UNLINK=%hu",&node_id)) {
        if (node_id == g_my_address) {
            printf("[UNLINK] Cannot unlink from self\n");
            return;
        }
        // disable link
        NodeEntry *linked_node = get_node_ptr(node_id);
        if (!linked_node) {
            printf("[UNLINK] Node with id = %hu not found\n",node_id);
            return;
        }

        ID unlink_msg = create_data_object(NO_ID, MAINTENANCE, "unlink", g_my_address, node_id, g_my_address, 0, 0, 0, NO_ID);
        queue_send(unlink_msg, node_id, false);
    }
}


void rquery_task(void *arg) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(RQUERY_INTERVAL_MS));
        ID msg = create_data_object(
            NO_ID, MAINTENANCE, "rquery",
            g_my_address, 0, g_my_address,
            0, 0, 0, NO_ID
        );
        queue_send(msg, 0, false);
    }
}

