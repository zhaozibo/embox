/**
 * @file
 * @brief
 *
 * @author Dmitry Pilyuk
 * @date 05.02.25
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <plc/core.h>
#include <plc/modbus_plc.h>

#include "toml.h"

static volatile int keep_running = 1;

void handle_signal(int sig) {
	keep_running = 0;
}

server_node_t *get_server_node(void) {
	char errbuf[200];

	FILE *f = fopen("modbus.toml", "r");
	if (!f) {
		fprintf(stderr, "ERROR: No such file\n");
		exit(1);
	}

	toml_table_t *tbl = toml_parse_file(f, errbuf, sizeof(errbuf));
	if (!tbl) {
		fprintf(stderr, "ERROR: %s\n", errbuf);
		exit(1);
	}

	fclose(f);

	toml_value_t host = toml_table_string(tbl, "host");
	toml_value_t port = toml_table_int(tbl, "port");
	toml_value_t mb_addr = toml_table_int(tbl, "mb_addr");
	toml_value_t addr_bits = toml_table_int(tbl, "addr_bits");
	toml_value_t nb_bits = toml_table_int(tbl, "nb_bits");
	toml_value_t start_bits = toml_table_int(tbl, "start_bits");
	toml_value_t addr_input_bits = toml_table_int(tbl, "addr_input_bits");
	toml_value_t nb_input_bits = toml_table_int(tbl, "nb_input_bits");
	toml_value_t start_input_bits = toml_table_int(tbl, "start_input_bits");
	toml_value_t addr_input_registers = toml_table_int(tbl, "addr_input_"
	                                                        "registers");
	toml_value_t nb_input_registers = toml_table_int(tbl, "nb_input_registers");
	toml_value_t start_input_registers = toml_table_int(tbl, "start_input_"
	                                                         "registers");
	toml_value_t addr_registers = toml_table_int(tbl, "addr_registers");
	toml_value_t nb_registers = toml_table_int(tbl, "nb_registers");
	toml_value_t start_registers = toml_table_int(tbl, "start_registers");

	server_node_t *node = malloc(sizeof(server_node_t));
	node->base_addr = 0;

	if (host.ok) {
		strcpy(node->host, host.u.s);
	}
	else {
		strcpy(node->host, "0.0.0.0");
	}

	node->port = (port.ok && port.u.i >= 0) ? port.u.i : 502;
	if (mb_addr.ok && mb_addr.u.i >= 0) {
		node->mb_addr = mb_addr.u.i;
	}
	else {
		fprintf(stderr, "ERROR: %s\n", errbuf);
		exit(1);
	}

	if (addr_bits.ok && addr_bits.u.i >= 0) {
		node->addr_bits = addr_bits.u.i;
	}
	else {
		fprintf(stderr, "ERROR: %s\n", errbuf);
		exit(1);
	}

	if (addr_input_bits.ok && addr_input_bits.u.i >= 0) {
		node->addr_input_bits = addr_input_bits.u.i;
	}
	else {
		fprintf(stderr, "ERROR: %s\n", errbuf);
		exit(1);
	}

	if (addr_input_registers.ok && addr_input_registers.u.i >= 0) {
		node->addr_input_registers = addr_input_registers.u.i;
	}
	else {
		fprintf(stderr, "ERROR: %s\n", errbuf);
		exit(1);
	}

	if (addr_registers.ok && addr_registers.u.i >= 0) {
		node->addr_registers = addr_registers.u.i;
	}
	else {
		fprintf(stderr, "ERROR: %s\n", errbuf);
		exit(1);
	}

	if (!nb_bits.ok || nb_bits.u.i < 0)
		nb_bits.u.i = 0;
	if (!start_bits.ok || start_bits.u.i < 0)
		start_bits.u.i = 0;
	if (!nb_input_bits.ok || nb_input_bits.u.i < 0)
		nb_input_bits.u.i = 0;
	if (!start_input_bits.ok || start_input_bits.u.i < 0)
		start_input_bits.u.i = 0;
	if (!nb_input_registers.ok || nb_input_registers.u.i < 0)
		nb_input_registers.u.i = 0;
	if (!start_input_registers.ok || start_input_registers.u.i < 0)
		start_input_registers.u.i = 0;
	if (!nb_registers.ok || nb_registers.u.i < 0)
		nb_registers.u.i = 0;
	if (!start_registers.ok && start_registers.u.i >= 0)
		start_registers.u.i = 0;

	node->ctx = modbus_new_tcp(host.u.s, port.u.i);
	if (!node->ctx) {
		fprintf(stderr, "Failed to create context: %s\n",
		    modbus_strerror(errno));
		exit(1);
	}

	node->mb_mapping = modbus_mapping_new_start_address(start_bits.u.i,
	    nb_bits.u.i, start_input_bits.u.i, nb_input_bits.u.i,
	    start_registers.u.i, nb_registers.u.i, start_input_registers.u.i,
	    nb_input_registers.u.i);
	if (!node->mb_mapping) {
		fprintf(stderr, "Failed to allocate the mapping: %s\n",
		    modbus_strerror(errno));
		modbus_free(node->ctx);
		exit(1);
	}

	return node;
}

void update_located_vars(server_node_t *node) {
	struct plc_located_var *var;
	plc_located_var_foreach(var) {
		if (var->n0 == node->base_addr && var->n1 == node->mb_addr) {
			if (var->n2 == node->addr_bits
			    && var->n3 >= node->mb_mapping->start_bits
			    && var->n3 < node->mb_mapping->start_bits
			                     + node->mb_mapping->nb_bits) {
				uint8_t *addr = (uint8_t *)var->addr;
				*addr = node->mb_mapping
				            ->tab_bits[var->n3 - node->mb_mapping->start_bits];
			}
			if (var->n2 == node->addr_input_bits
			    && var->n3 >= node->mb_mapping->start_input_bits
			    && var->n3 < node->mb_mapping->start_input_bits
			                     + node->mb_mapping->nb_input_bits) {
				uint8_t *addr = (uint8_t *)var->addr;
				*addr =
				    node->mb_mapping
				        ->tab_input_bits[var->n3
				                         - node->mb_mapping->start_input_bits];
			}
			if (var->n2 == node->addr_input_registers
			    && var->n3 >= node->mb_mapping->start_input_registers
			    && var->n3 < node->mb_mapping->start_input_registers
			                     + node->mb_mapping->nb_input_registers) {
				uint16_t *addr = (uint16_t *)var->addr;
				*addr = node->mb_mapping->tab_input_registers
				            [var->n3 - node->mb_mapping->start_input_registers];
			}
			if (var->n2 == node->addr_registers
			    && var->n3 >= node->mb_mapping->start_registers
			    && var->n3 < node->mb_mapping->start_registers
			                     + node->mb_mapping->nb_registers) {
				uint16_t *addr = (uint16_t *)var->addr;
				*addr =
				    node->mb_mapping
				        ->tab_registers[var->n3
				                        - node->mb_mapping->start_registers];
			}
		}
	}
}

void update_mb_mapping(server_node_t *node) {
	struct plc_located_var *var;
	plc_located_var_foreach(var) {
		if (var->n0 == node->base_addr && var->n1 == node->mb_addr) {
			if (var->n2 == node->addr_bits
			    && var->n3 >= node->mb_mapping->start_bits
			    && var->n3 < node->mb_mapping->start_bits
			                     + node->mb_mapping->nb_bits) {
				node->mb_mapping
				    ->tab_bits[var->n3 - node->mb_mapping->start_bits] = *(
				    uint8_t *)var->addr;
			}
			if (var->n2 == node->addr_input_bits
			    && var->n3 >= node->mb_mapping->start_input_bits
			    && var->n3 < node->mb_mapping->start_input_bits
			                     + node->mb_mapping->nb_input_bits) {
				node->mb_mapping
				    ->tab_input_bits[var->n3
				                     - node->mb_mapping->start_input_bits] =
				    *(uint8_t *)var->addr;
			}
			if (var->n2 == node->addr_input_registers
			    && var->n3 >= node->mb_mapping->start_input_registers
			    && var->n3 < node->mb_mapping->start_input_registers
			                     + node->mb_mapping->nb_input_registers) {
				node->mb_mapping->tab_input_registers
				    [var->n3 - node->mb_mapping->start_input_registers] = *(
				    uint16_t *)var->addr;
			}
			if (var->n2 == node->addr_registers
			    && var->n3 >= node->mb_mapping->start_registers
			    && var->n3 < node->mb_mapping->start_registers
			                     + node->mb_mapping->nb_registers) {
				node->mb_mapping
				    ->tab_registers[var->n3 - node->mb_mapping->start_registers] =
				    *(uint8_t *)var->addr;
			}
		}
	}
}

int modbus_server(void) {
	int listen_socket;

	signal(SIGKILL, handle_signal);

	server_node_t *node = get_server_node();

	listen_socket = modbus_tcp_listen(node->ctx, 1);
	if (listen_socket == -1) {
		fprintf(stderr, "Failed to listen: %s\n", modbus_strerror(errno));
		modbus_mapping_free(node->mb_mapping);
		modbus_free(node->ctx);
		free(node);
		return -1;
	}

	while (keep_running) {
		int client_socket = modbus_tcp_accept(node->ctx, &listen_socket);
		if (client_socket == -1) {
			break;
		}
		while (keep_running) {
			uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];

			int rc = modbus_receive(node->ctx, query);
			update_mb_mapping(node);
			if (rc > 0) {
				modbus_reply(node->ctx, query, rc, node->mb_mapping);
			}
			else if (rc == -1) {
				/* Connection closed by the client or error */
				break;
			}
			update_located_vars(node);
		}
		close(client_socket);
	}
	if (listen_socket != -1) {
		close(listen_socket);
	}
	printf("ENDING MODBUS\n");

	free(node->mb_mapping);
	free(node->ctx);
	free(node);
	return 0;
}
