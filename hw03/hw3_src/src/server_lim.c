/*******************************************************************************
* Dual-Threaded FIFO Server Implementation w/ Queue Limit
*
* Description:
*     A server implementation designed to process client requests in First In,
*     First Out (FIFO) order. The server binds to the specified port number
*     provided as a parameter upon launch. It launches a secondary thread to
*     process incoming requests and allows to specify a maximum queue size.
*
* Usage:
*     <build directory>/server -q <queue_size> <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*     queue_size  - The maximum number of queued requests
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 29, 2023
*
* Notes:
*     Ensure to have proper permissions and available port before running the
*     server. The server relies on a FIFO mechanism to handle requests, thus
*     guaranteeing the order of processing. If the queue is full at the time a
*     new request is received, the request is rejected with a negative ack.
*
*******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>

/* Needed for wait(...) */
#include <sys/types.h>
#include <sys/wait.h>

/* Needed for semaphores */
#include <semaphore.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"

#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s -q <queue size> <port_number>\n"

/* 4KB of stack for the worker thread */
#define STACK_SIZE (4096)

/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */

struct request_meta {
	struct request request;
	struct timespec receipt_timestamp;
	struct timespec start_timestamp;
	struct timespec completion_timestamp;
};

struct queue {
	struct request_meta* items;
    int capacity; 
    int front;
    int rear;
    int size;
};

struct connection_params {
	int queue_size;
};

struct worker_params {
	int worker_done;
    struct queue *the_queue;
	int conn_socket;
	struct timespec receipt_timestamp;
};

/* Helper function to perform queue initialization */
void queue_init(struct queue * the_queue, size_t queue_size)
{
	the_queue->items = (struct request_meta*)malloc(queue_size * sizeof(struct request_meta));
    the_queue->capacity = queue_size;
    the_queue->size = 0; 
    the_queue->front = 0;
    the_queue->rear = -1;
}

/* Add a new request <request> to the shared queue <the_queue> */
int add_to_queue(struct request_meta to_add, struct queue * the_queue)
{
	int retval = 0;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* Make sure that the queue is not full */
	if (the_queue->size == the_queue->capacity) {
		/* What to do in case of a full queue */
		retval = 1;
	} else {
		/* If all good, add the item in the queue */
		the_queue->rear = (the_queue->rear + 1) % the_queue->capacity;
		the_queue->items[the_queue->rear] = to_add;
		the_queue->size++;
		/* QUEUE SIGNALING FOR CONSUMER --- DO NOT TOUCH */
		sem_post(queue_notify);
	}

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Add a new request <request> to the shared queue <the_queue> */
struct request_meta get_from_queue(struct queue * the_queue)
{
	struct request_meta retval;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_notify);
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	if (the_queue->size > 0) {
        retval = the_queue->items[the_queue->front];
        the_queue->front = (the_queue->front + 1) % the_queue->capacity;
        the_queue->size--;
    }
    else {
        perror("Error: The queue is empty!");
    }
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

void dump_queue_status(struct queue * the_queue)
{
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	printf("Q:[");
	for (int i = 0; i < the_queue->size; i++) {
		uint64_t current_id = the_queue->items[(the_queue->front + i) % the_queue->capacity].request.req_id;
		printf("R%lu", current_id);
		if (i < the_queue->size - 1) {
			printf(",");
		}
	}
	printf("]\n");
	
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
}

/* Main logic of the worker thread */
int worker_main (void * arg)
{
	struct timespec now, start_timestamp, completion_timestamp;
	struct worker_params * params = (struct worker_params *)arg;
	struct response resp;
	struct queue* the_queue = params->the_queue;
	int conn_socket = params->conn_socket;

	/* Print the first alive message. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	printf("[#WORKER#] %lf Worker Thread Alive!\n", TSPEC_TO_DOUBLE(now));

	/* Okay, now execute the main logic. */
	while (!params->worker_done) {
		struct request_meta req = get_from_queue(the_queue);
		clock_gettime(CLOCK_MONOTONIC, &req.start_timestamp);
		busywait_timespec(req.request.req_length);
		clock_gettime(CLOCK_MONOTONIC, &req.completion_timestamp);

		resp.req_id = req.request.req_id;
		resp.ack = 0;
		
		size_t send_response = send(conn_socket, &resp, sizeof(struct response), 0);
		if (send_response == -1) {
			perror("Error sending response \n");
			break;
		}

		printf("R%ld:%lf,%lf,%lf,%lf,%lf\n", 
        req.request.req_id, 
        TSPEC_TO_DOUBLE(req.request.req_timestamp),
        TSPEC_TO_DOUBLE(req.request.req_length),
        TSPEC_TO_DOUBLE(req.receipt_timestamp), 
        TSPEC_TO_DOUBLE(req.start_timestamp),
        TSPEC_TO_DOUBLE(req.completion_timestamp));

		dump_queue_status(the_queue);
	}

	return EXIT_SUCCESS;
}

/* This function will start the worker thread wrapping around the
 * clone() system call*/
int start_worker(void * params, void * worker_stack)
{
    int pid = clone(worker_main, (char*)worker_stack + STACK_SIZE,  CLONE_THREAD | CLONE_VM | CLONE_SIGHAND | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM, params);
    return pid;
}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket, struct connection_params conn_params)
{
	struct request_meta * req;
	struct queue * the_queue;
	size_t in_bytes;
	int worker_id;
	int  worker_done = 0;

	/* The connection with the client is alive here. Let's get
	 * ready to start the worker thread. */
	void * worker_stack = malloc(STACK_SIZE);
	struct worker_params worker_params;

	struct response resp;
	req = (struct request_meta *)malloc(sizeof(struct request_meta));

	/* Now handle queue allocation and initialization */

	the_queue = (struct queue *)malloc(sizeof(struct queue));
	worker_params.worker_done = worker_done;  
	worker_params.the_queue = the_queue;
	
	worker_params.conn_socket = conn_socket;

    if (the_queue == NULL) {
        perror("Failed to allocate memory for the queue");
        exit(EXIT_FAILURE);
    }
    queue_init(the_queue, conn_params.queue_size);


	/* Prepare worker_parameters */
	if(worker_stack == NULL){
		perror("ERROR: Unable to allocate memory.\n");
      	exit(EXIT_FAILURE);
	}
	worker_id = start_worker(&worker_params, worker_stack);

	if (worker_id < 0) {
		perror("ERROR: Unable to create the child process.\n");
        exit(EXIT_FAILURE);
	}

	printf("INFO: Worker thread started. Thread ID = %d\n", worker_id);

	/* We are ready to proceed with the rest of the request
	 * handling logic. */
	req = (struct request_meta *)malloc(sizeof(struct request_meta));

	do {
		in_bytes = recv(conn_socket, &req->request, sizeof(struct request), 0);
		/* Don't just return if in_bytes is 0 or -1. Instead
         * skip the response and break out of the loop in an
         * orderly fashion so that we can de-allocate the req
         * and resp varaibles, and shutdown the socket. */

		clock_gettime(CLOCK_MONOTONIC, &req->receipt_timestamp);
		if (in_bytes < 0) {
			break;
		}

		if (add_to_queue(*req, the_queue) == 1) { 
			resp.req_id = req->request.req_id;  
			resp.ack = 1;
			printf("X%lu:%lf,%lf,%lf\n", 
                resp.req_id, 
                TSPEC_TO_DOUBLE(req->request.req_timestamp), 
                TSPEC_TO_DOUBLE(req->request.req_length), 
                TSPEC_TO_DOUBLE(req->receipt_timestamp) );
			
			send(conn_socket, &resp, sizeof(struct response), 0);
		}
	} while (in_bytes > 0);

	/* Ask the worker thead to terminate */
	printf("INFO: Asserting termination flag for worker thread...\n");
	worker_params.worker_done = 1;

	/* Just in case the thread is stuck on the notify semaphore,
	 * wake it up */
	sem_post(queue_notify);

	/* Wait for orderly termination of the worker thread */
	waitpid(-1, NULL, 0);
	printf("INFO: Worker thread exited.\n");
	free(worker_stack);
	free(the_queue);

	free(req);
	shutdown(conn_socket, SHUT_RDWR);
	close(conn_socket);
	printf("INFO: Client disconnected.\n");
}


/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval, opt, queue_size;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;

	struct connection_params conn_params;

	/* Parse all the command line arguments */
	while ((opt = getopt(argc, argv, "q:")) != -1) {
        switch (opt) {
            case 'q':
                queue_size = atoi(optarg);
                break;
            case '?':
                if (optopt == 'q') {
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                } else {
                    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                }
                return EXIT_FAILURE;
            default:
				fprintf(stderr, USAGE_STRING, argv[0]);
                return EXIT_FAILURE;
        }
    }

	/* PARSE THE COMMANDS LINE: */
	/* 1. Detect the -q parameter and set aside the queue size in conn_params */
	conn_params.queue_size = queue_size;
	/* 2. Detect the port number to bind the server socket to (see HW1 and HW2) */
	if (optind < argc) {
        socket_port = atoi(argv[optind]);
    } else {
        fprintf(stderr, USAGE_STRING, argv[0]);
        return EXIT_FAILURE;
    }

	/* Now onward to create the right type of socket */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		ERROR_INFO();
		perror("Unable to create socket");
		return EXIT_FAILURE;
	}

	/* Before moving forward, set socket to reuse address */
	optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval));

	/* Convert INADDR_ANY into network byte order */
	any_address.s_addr = htonl(INADDR_ANY);

	/* Time to bind the socket to the right port  */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(socket_port);
	addr.sin_addr = any_address;

	/* Attempt to bind the socket with the given parameters */
	retval = bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to bind socket");
		return EXIT_FAILURE;
	}

	/* Let us now proceed to set the server to listen on the selected port */
	retval = listen(sockfd, BACKLOG_COUNT);

	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to listen on socket");
		return EXIT_FAILURE;
	}

	/* Ready to accept connections! */
	printf("INFO: Waiting for incoming connection...\n");
	client_len = sizeof(struct sockaddr_in);
	accepted = accept(sockfd, (struct sockaddr *)&client, &client_len);

	if (accepted == -1) {
		ERROR_INFO();
		perror("Unable to accept connections");
		return EXIT_FAILURE;
	}

	/* Initialize queue protection variables. DO NOT TOUCH. */
	queue_mutex = (sem_t *)malloc(sizeof(sem_t));
	queue_notify = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(queue_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue mutex");
		return EXIT_FAILURE;
	}
	retval = sem_init(queue_notify, 0, 0);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue notify");
		return EXIT_FAILURE;
	}
	/* DONE - Initialize queue protection variables */

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted, conn_params);

	free(queue_mutex);
	free(queue_notify);

	close(sockfd);
	return EXIT_SUCCESS;

}
