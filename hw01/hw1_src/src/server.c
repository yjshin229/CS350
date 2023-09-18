/*******************************************************************************
* Simple FIFO Order Server Implementation
*
* Description:
*     A server implementation designed to process client requests in First In,
*     First Out (FIFO) order. The server binds to the specified port number
*     provided as a parameter upon launch.
*
* Usage:
*     <build directory>/server <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 10, 2023
*
* Notes:
*     Ensure to have proper permissions and available port before running the
*     server. The server relies on a FIFO mechanism to handle requests, thus
*     guaranteeing the order of processing. For debugging or more details, refer
*     to the accompanying documentation and logs.
*
*******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"

#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s <port_number>\n"

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
static void handle_connection(int conn_socket)
{
	uint64_t request_id;
	struct timespec receipt_time ,client_timestamp, request_length, completion_time;
	int exit = 0;

	while(exit == 0){
		/* check if the requestId, client timestamp and the length
		was received successfully in consecutive order.
		*/ 
		ssize_t received_id = recv(conn_socket, &request_id, sizeof(request_id), 0);
		ssize_t received_client = recv(conn_socket, &client_timestamp, sizeof(client_timestamp), 0);
		ssize_t received_request_length = recv(conn_socket, &request_length, sizeof(request_length), 0);

		//if either information is not received, print an error message and break.
		if (received_id <= 0 || received_client <= 0 || received_request_length <= 0 ) {
            perror("Error receiving request ID or client timestamp or request length\n");
			exit = 1;
            break;
        }
        
		//get the timestamp at which the server received the request.
		clock_gettime(CLOCK_MONOTONIC, &receipt_time);

		//perform a busy wait for the amount of requested length time.
		get_elapsed_busywait(request_length.tv_sec, request_length.tv_nsec);

		//send response back to the client
		ssize_t send_response = send(conn_socket, &request_id, sizeof(request_id), 0);
		
		if (send_response == -1) {
			perror("Error sending response \n");
			exit = 1;
			break;
		}else{
			//get the timestamp at which the server completed processing the request.	
			clock_gettime(CLOCK_MONOTONIC, &completion_time);
		}

		float sent_time = ((long)client_timestamp.tv_sec * NANO_IN_SEC +  client_timestamp.tv_nsec)/(float) 1e9;
		float request_time = ((long)request_length.tv_sec * NANO_IN_SEC + request_length.tv_nsec)/(float)1e9;
		
		if(exit == 0){
			printf("R%lu:%ld.%09ld,%ld.%09ld,%ld.%09ld,%ld.%09ld\n", request_id, client_timestamp.tv_sec, client_timestamp.tv_nsec, request_length.tv_sec, request_length.tv_nsec, receipt_time.tv_sec, receipt_time.tv_nsec,completion_time.tv_sec,completion_time.tv_nsec);
		}

		

	}

	


}

/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;

	/* Get port to bind our socket to */
	if (argc > 1) {
		socket_port = strtol(argv[1], NULL, 10);
		printf("INFO: setting server port as: %d\n", socket_port);
	} else {
		ERROR_INFO();
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

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted);

	close(sockfd);
	return EXIT_SUCCESS;

}
