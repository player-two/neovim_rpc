#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// needed for cmp.h
#include <stdint.h>
#include <stdbool.h>

#include "cmp.h"
#include "rpc.h"
#include "socket.h"

uint8_t req_id = 0;

enum rpc_connection_method {
  STDIN_STDOUT, // for use with nvim's jobstart(_, {rpc: v:true})
  TCP_SOCKET, // NVIM_LISTEN_ADDRESS
  NAMED_SOCKET, // echo v:servername
  EMBEDDED
};

bool stdin_reader (cmp_ctx_t *cmp, void *data, size_t limit) {
  return fread(data, 1, limit, stdin) == limit;
}

size_t stdout_writer (cmp_ctx_t *cmp, const void *data, size_t count) {
  return fwrite(data, 1, count, stdout);
}

void rpc_init_stdio (void) {
  cmp_init(&cmp, NULL, stdin_reader, stdout_writer); // cmp->buf should never be used
}

void rpc_init_socket (char name[]) {
  int *sock = malloc(sizeof(int));
  *sock = make_named_socket(name, false);
  cmp = msgpack_socket_init(sock);
}

void rpc_end () {
  close(*(int *) cmp.buf);
  free(cmp.buf);
}

bool rpc_send (rpc_type t, char method[], int num_args) {
  if (!cmp_write_array(&cmp, 4)) {
    return false;
  }
  if (!cmp_write_uint(&cmp, 0)) {
    return false;
  }
  if (!cmp_write_uint(&cmp, req_id)) {
    return false;
  }
  if (!cmp_write_str(&cmp, method, strlen(method))) {
    return false;
  }
  if (!cmp_write_array(&cmp, num_args)) {
    return false;
  }
  return true;
}

bool read_message_headers () {
  uint8_t id, type;
  uint32_t array_size;
  if (!cmp_read_array(&cmp, &array_size)) {
    printf("expected array\n");
    return false;
  }

  if (array_size != 3 && array_size != 4) {
    printf("array too small: %i\n", array_size);
    return false;
  }

  if (!cmp_read_pfix(&cmp, &type)) {
    printf("expected pfix\n");
    return false;
  }

  if (!cmp_read_pfix(&cmp, &id)) {
    printf("expected pfix\n");
    return false;
  }

  if (!cmp_read_nil(&cmp)) {
    // TODO: set result as an error
    printf("expected nil\n");
    return false;
  }

  return true;
}

bool wait_for_response (rpc_message *msg) {
  uint8_t num_read = 0;
  while (true) {
    num_read++;
    if (!read_message(msg)) {
      return false;
    }
    if (msg->id == req_id) {
      return true;
    }
    if (num_read > 10) {
      msg = NULL;
      return false;
    }
  }
}

bool read_message (rpc_message *msg) {
  uint8_t type;
  uint32_t array_size;
  if (!cmp_read_array(&cmp, &array_size)) {
    printf("expected array\n");
    return false;
  }

  if (array_size != 3 && array_size != 4) {
    printf("array too small: %i\n", array_size);
    return false;
  }

  if (!cmp_read_pfix(&cmp, &type)) {
    printf("expected pfix\n");
    return false;
  }
  msg->type = (rpc_type) type;

  if (!cmp_read_pfix(&cmp, &msg->id)) {
    printf("expected pfix\n");
    return false;
  }

  if (!cmp_read_nil(&cmp)) {
    // TODO: set result as an error
    printf("expected nil\n");
    return false;
  }

  if (!cmp_read_array(&cmp, &msg->size)) {
    printf("expected array\n");
    return false;
  }

  if (msg->size == 0) {
    return true;
  }

  int8_t ext_type;
  uint32_t ext_size;
  if (!cmp_read_ext_marker(&cmp, &ext_type, &ext_size)) {
    return false;
  }
  msg->data = malloc(msg->size * ext_size);

  if (!cmp.read(&cmp, msg->data, ext_size)) {
    return false;
  }

  for (int i = 1; i < msg->size; i++) {
    if (!cmp_read_ext(&cmp, &ext_type, &ext_size, msg->data + i * ext_size)) {
      return false;
    }
  }

  return true;
}

bool read_string (char *result) {
  uint32_t str_size;
  if (!cmp_read_str_size(&cmp, &str_size)) {
    return false;
  }
  result = malloc(str_size * sizeof(char));
  if (!cmp.read(&cmp, result, str_size)) {
    return false;
  }
  return true;
}