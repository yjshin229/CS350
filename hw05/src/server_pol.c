/*******************************************************************************
* Multi-Threaded FIFO Server Implementation w/ Queue Limit
*
* Description:
*     A server implementation designed to process client requests in First In,
*     First Out (FIFO) order. The server binds to the specified port number
*     provided as a parameter upon launch. It launches multiple threads to
*     process incoming requests and allows to specify a maximum queue size.
*
* Usage:
*     <build directory>/server -q <queue_size> -w <workers> <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*     queue_size  - The maximum number of queued requests
*     workers     - The number of workers to start to process requests
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
	"Usage: %s -q <queue size> "		\
	"-w <workers> "				\
	"-p <policy: FIFO | SJN> "		\
	"<port_number>\n"

/* 4KB of stack for the worker thread */
#define STACK_SIZE (4096)

/* Mutex needed to protect the threaded printf. DO NOT TOUCH */
sem_t * printf_mutex;

/* Synchronized printf for multi-threaded operation */
#define sync_printf(...)			\
	do {					\
		sem_wait(printf_mutex);		\
		printf(__VA_ARGS__);		\
		sem_post(printf_mutex);		\
	} while (0)

/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */


enum queue_policy {
	QUEUE_FIFO,
	QUEUE_SJN
};

struct request_meta {
	struct request request;
	struct timespec receipt_timestamp;
	struct timespec start_timestamp;
	struct timespec completion_timestamp;
	enum queue_policy policy;
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
	int workers;
	enum queue_policy policy;
};

struct worker_params {
	int worker_done;
    struct queue *the_queue;
	int conn_socket;
	int thread_id;
	int worker_id;
};

/* Helper function to perform queue initialization */
void queue_init(struct queue * the_queue, size_t queue_size)
{
	the_queue->items = (struct request_meta*)malloc(queue_size * sizeof(struct request_meta));
    the_queue->capacity = queue_size;
    the_queue->size = 0; 
    the_queue->front = 0;
    the_queue->rear = 0;
}

/* Add a new request <request> to the shared queue <the_queue> */
int add_to_queue(struct request_meta to_add, struct queue * the_queue)
{
	// sync_printf("Im in add_to_queue!\n");
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
		/* The sem_post(queue_mutex) below MUST happen. */
	} else {
		/* If all good, add the item in the queue */

		if (to_add.policy == QUEUE_FIFO) {
            the_queue->items[the_queue->rear] = to_add;
            the_queue->rear = (the_queue->rear + 1) % the_queue->capacity;
            the_queue->size++;
		}else if(to_add.policy == QUEUE_SJN){
			// int i;
			// for (i = the_queue->front; i != the_queue->rear; i = (i + 1) % the_queue->capacity) {
			// 	if (timespec_cmp(&to_add.request.req_length, &the_queue->items[i].request.req_length) < 0) {
			// 		the_queue->items[(i + 1) % the_queue->capacity] = the_queue->items[i];
			// 	} else {
			// 		break;
			// 	}
			// }
			// the_queue->items[i] = to_add;
			// the_queue->rear = (the_queue->rear + 1) % the_queue->capacity;
			// the_queue->size++;
			// sync_printf("Added to queue!\n");

			int i;
			for (i = 0; i < the_queue->size; ++i) {
				int idx = (the_queue->front + i) % the_queue->capacity;
				// Assuming we are comparing based on tv_sec for simplicity
				// Adjust as necessary for your criteria
				if (timespec_cmp(&to_add.request.req_length, &the_queue->items[i].request.req_length) < 0) {
					break;
				}
			}

			// i now holds the position where we want to insert to_add

			// Shift all longer requests by one spot
			for (int j = the_queue->size; j > i; --j) {
				int from = (the_queue->front + j - 1) % the_queue->capacity;
				int to = (the_queue->front + j) % the_queue->capacity;
				the_queue->items[to] = the_queue->items[from];
			}

			// Insert the new request
			int insert_pos = (the_queue->front + i) % the_queue->capacity;
			the_queue->items[insert_pos] = to_add;
			the_queue->rear = (the_queue->rear + 1) % the_queue->capacity;
			the_queue->size++;
		}
		// for (i = the_queue->front; i != the_queue->rear; i = (i + 1) % the_queue->capacity) {
			// 	sync_printf("current index: %d\n", i);
			// 	if (timespec_cmp(&to_add.request.req_length, &the_queue->items[i].request.req_length) < 0) {
			// 		sync_printf("Index to insert: %d\n", i);
			// 		break;
			// 	}
			// }

			// for (int j = the_queue->rear; j != i - 1; j = (j - 1 + the_queue->capacity) % the_queue->capacity) {
			// 	the_queue->items[(j + 1) % the_queue->capacity] = the_queue->items[j];
			// }

			// the_queue->items[i] = to_add;
			// the_queue->rear = (the_queue->rear + 1) % the_queue->capacity;
			// the_queue->size++;

		/* OPTION 3: Do nothing different from FIFO case,
		 * and deal with the SJN policy at dequeue time.*/

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

	// sync_printf("The size of the queue: %d\n", the_queue->size);
	if (the_queue->size > 0) {
        retval = the_queue->items[the_queue->front];
        the_queue->front = (the_queue->front + 1) % the_queue->capacity;
        the_queue->size--;
    }
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* Option 3-A: Scan the queue to find the shortest request,
	 * pop it, and shift all the other requests by one spot to the
	 * left. */

	/* Option 3-B: Sort the entire queue and pop the request in
	 * the first position. */

	/* NOTE: all these options only apply if you have implemented
	 * your queue as an array. If you have employed a linked list,
	 * the sorted insert approach is definitely the winner. Also,
	 * in this case you are a wizard, Harry. */

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

	// sync_printf("In DumpQueue!! queue size: %d\n", the_queue->size);

	sync_printf("Q:[");
    for (int i = 0; i < the_queue->size; i++) {
		uint64_t current_id = the_queue->items[(the_queue->front + i) % the_queue->capacity].request.req_id;
        sync_printf("R%lu", current_id);
        if (i < the_queue->size - 1) {
            sync_printf(",");
        }
    }
    sync_printf("]\n");
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
}

/* Main logic of the worker thread */
int worker_main (void * arg)
{
	struct timespec now;
	struct worker_params * params = (struct worker_params *)arg;
	struct response resp;
	struct queue* the_queue = params->the_queue;
	int conn_socket = params->conn_socket;

	/* Print the first alive message. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	// sync_printf("[#WORKER#] %lf Worker Thread Alive!\n", TSPEC_TO_DOUBLE(now));

	/* Okay, now execute the main logic. */
	while (!params->worker_done) {
		// sync_printf("In worker main before getting request\n");
		struct request_meta req = get_from_queue(the_queue);
		// sync_printf("In worker main after getting request. Req id: %d\n", req.request.req_id);

		clock_gettime(CLOCK_MONOTONIC, &req.start_timestamp);
		busywait_timespec(req.request.req_length);
		clock_gettime(CLOCK_MONOTONIC, &req.completion_timestamp);
		
		resp.req_id = req.request.req_id;
		resp.ack = RESP_COMPLETED;

		size_t send_response = send(conn_socket, &resp, sizeof(struct response), 0);
		if (send_response < 0) {
			perror("Error sending response \n");
			break;
		}
		// sync_printf("Sent Response Id: %d\n",resp.req_id);

		sync_printf("T%d R%lu:%lf,%lf,%lf,%lf,%lf\n", 
			params->thread_id, 
			req.request.req_id, 
			TSPEC_TO_DOUBLE(req.request.req_timestamp),
			TSPEC_TO_DOUBLE(req.request.req_length), 
			TSPEC_TO_DOUBLE(req.receipt_timestamp),
			TSPEC_TO_DOUBLE(req.start_timestamp),
			TSPEC_TO_DOUBLE(req.completion_timestamp)
		);
		// sync_printf("%lf,%lf,%lf,%lf,%lf\n", 
		// 	TSPEC_TO_DOUBLE(req.request.req_timestamp),
		// 	TSPEC_TO_DOUBLE(req.request.req_length), 
		// 	TSPEC_TO_DOUBLE(req.receipt_timestamp),
		// 	TSPEC_TO_DOUBLE(req.start_timestamp),
		// 	TSPEC_TO_DOUBLE(req.completion_timestamp)
		// );
		dump_queue_status(params->the_queue);
		// sync_printf("request done\n");
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
	struct response resp;

	int worker_id;

	the_queue = (struct queue *)malloc(sizeof(struct queue));
	if (the_queue == NULL) {
        perror("Failed to allocate memory for the queue");
        exit(EXIT_FAILURE);
    }
    queue_init(the_queue, conn_params.queue_size);

	struct worker_params * worker_params = malloc(sizeof(struct worker_params) * conn_params.workers);
	void** worker_stacks = malloc(conn_params.workers * sizeof(void *));
	
	for (int i = 0; i < conn_params.workers; i++) {
		worker_stacks[i] = malloc(STACK_SIZE);
		if(worker_stacks == NULL){
			perror("ERROR: Unable to allocate memory.\n");
      		exit(EXIT_FAILURE);
		}
		if (worker_id < 0) {
            perror("ERROR: Unable to create the child process.\n");
            exit(EXIT_FAILURE);
        }

        worker_params[i].conn_socket = conn_socket;
		worker_params[i].worker_done = 0;
        worker_params[i].the_queue = the_queue;
		worker_params[i].thread_id = i;
		worker_id = start_worker(&worker_params[i], worker_stacks[i]);
		worker_params[i].worker_id = worker_id;

		// sync_printf("INFO: Worker thread started. Thread ID = %d\n", worker_id);
    }


	/* We are ready to proceed with the rest of the request
	 * handling logic. */

	req = (struct request_meta *)malloc(sizeof(struct request_meta));
	req->policy = conn_params.policy;
	do {
		in_bytes = recv(conn_socket, &req->request, sizeof(struct request), 0);
		// sync_printf("The current request Id (handle connection): %d\n", req->request.req_id);
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
			resp.ack = RESP_REJECTED;
			sync_printf("X%lu:%lf,%lf,%lf\n", 
                resp.req_id, 
                TSPEC_TO_DOUBLE(req->request.req_timestamp), 
                TSPEC_TO_DOUBLE(req->request.req_length), 
                TSPEC_TO_DOUBLE(req->receipt_timestamp));
			
			send(conn_socket, &resp, sizeof(struct response), 0);
		}
	} while (in_bytes > 0);

	for (int i = 0; i < conn_params.workers; ++i) {
		worker_params[i].worker_done = 1;
		sem_post(queue_notify);
		waitpid(worker_params[i].worker_id, NULL, 0);
    }

	for(int i = 0; i < conn_params.workers; ++i){
		free(worker_stacks[i]);
	}

	free(the_queue);

	free(req);
	shutdown(conn_socket, SHUT_RDWR);
	close(conn_socket);
	// sync_printf("INFO: Client disconnected.\n");
}


/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval, opt, queue_size, workers;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;

	struct connection_params conn_params;

	/* Parse all the command line arguments */
	while ((opt = getopt(argc, argv, "q:w:p:")) != -1) {
        switch (opt) {
            case 'q':
                queue_size = atoi(optarg);
                break;
            case 'w':
                workers = atoi(optarg);
                break;
            case 'p':
                if (strcmp(optarg, "FIFO") == 0) {
                    conn_params.policy = QUEUE_FIFO;
                } else if (strcmp(optarg, "SJN") == 0) {
                    conn_params.policy = QUEUE_SJN;
                } else {
                    fprintf(stderr, "Invalid queue policy: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                fprintf(stderr, USAGE_STRING, argv[0]);
                exit(EXIT_FAILURE);
        }
    }
	/* PARSE THE COMMANDS LINE: */
	/* 1. Detect the -q parameter and set aside the queue size in conn_params */
	conn_params.queue_size = queue_size;
	/* 2. Detect the -w parameter and set aside the number of threads to launch */
	conn_params.workers = workers;
	/* 3. Detect the port number to bind the server socket to (see HW1 and HW2) */
	socket_port = atoi(argv[optind]);

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
	// printf("INFO: Waiting for incoming connection...\n");
	client_len = sizeof(struct sockaddr_in);
	accepted = accept(sockfd, (struct sockaddr *)&client, &client_len);

	if (accepted == -1) {
		ERROR_INFO();
		perror("Unable to accept connections");
		return EXIT_FAILURE;
	}

	/* Initilize threaded printf mutex */
	printf_mutex = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(printf_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize printf mutex");
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
