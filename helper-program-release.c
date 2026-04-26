#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PERMS 0666
#define CONSTANT 100000000

#define MAX_WALLETS_PER_BLOCK 100
#define WALLET_HASH_SIZE 16

struct timeval start, stop;

struct Block {
    int wallet_count;
    char wallet_hashes[MAX_WALLETS_PER_BLOCK][WALLET_HASH_SIZE + 1];
};

struct RecvMessageBuffer {
    long mtype;
    int sum;
};

struct SendMessageBuffer {
    long mtype;
    int security_value;
    int decryption_key;
};

int main(int argc, char* argv[]) {
    srand(time(0));
    key_t shmKey = rand() % CONSTANT;

    int blockSize, totalTransactions;
    char blocksFileName[25];
    sprintf(blocksFileName, "blocks_%s.bin", argv[1]);
    FILE* blocksFile = fopen(blocksFileName, "r");

    fscanf(blocksFile, "%d", &totalTransactions);
    fscanf(blocksFile, "%d", &blockSize);

    int shmid;
    struct Block* shmPtr;
    if ((shmid = shmget(shmKey, sizeof(struct Block) * blockSize, PERMS | IPC_CREAT)) == -1) {
        perror("Error in shmget");
        exit(1);
    }
    if ((shmPtr = shmat(shmid, NULL, 0)) == (void*)-1) {
        perror("Error in shmat");
        exit(1);
    }

    // Allocate arrays to hold security values and new decryption keys
    int security_values[blockSize + 1];
    int decryption_keys[blockSize + 1];

    // Read block data, including the new separate key
    for (int i = 0; i < blockSize; i++) {
        fscanf(blocksFile, "%d %d %d", &shmPtr[i].wallet_count, &security_values[i], &decryption_keys[i]);
        for (int j = 0; j < shmPtr[i].wallet_count; j++) {
            fscanf(blocksFile, "%s", shmPtr[i].wallet_hashes[j]);
        }
    }
    fclose(blocksFile);

    // Read answers file
    int ansArrayLength;
    char answerFileName[25];
    sprintf(answerFileName, "answers_%s.bin", argv[1]);
    FILE* answerFile = fopen(answerFileName, "r");
    fscanf(answerFile, "%d", &ansArrayLength);
    int* ansArray = (int*)malloc(sizeof(int) * ansArrayLength);
    for (int i = 0; i < ansArrayLength; i++) {
        fscanf(answerFile, "%d", &ansArray[i]);
    }
    fclose(answerFile);

    // Create and write to the input file for the solution program
    key_t msgKey = rand() % CONSTANT;
    char inputFileName[20];
    sprintf(inputFileName, "input_%s.txt", argv[1]);
    FILE* inputFile = fopen(inputFileName, "w");
    fprintf(inputFile, "%d\n%d\n%d\n%d", totalTransactions, blockSize, shmKey, msgKey);
    fclose(inputFile);

    // Create the message queue
    int msgId;
    if ((msgId = msgget(msgKey, PERMS | IPC_CREAT)) == -1) {
        perror("Error in msgget");
        exit(1);
    }

    // Start timer and fork the solution process
    gettimeofday(&start, NULL);
    int childId = fork();

    if(childId == -1){
        perror("Error while forking");
        exit(1);
    }

    if(childId == 0){
        if(execlp("./solution", "solution", argv[1], NULL) == -1){
            perror("Error in execlp");
            exit(1);
        }
    }

    struct SendMessageBuffer sentMessage;
    sentMessage.mtype = 2; // student receives on mtype=2
    sentMessage.security_value = security_values[0];
    sentMessage.decryption_key = decryption_keys[0];
    if (msgsnd(msgId, &sentMessage, sizeof(sentMessage) - sizeof(long), 0) == -1) {
        perror("Error in initial msgsnd");
        exit(1);
    }

    int index = 0, error = 0;
    struct RecvMessageBuffer recievedMessage;

    // 2. Loop to validate answers and send the next set of data
    while (index < ansArrayLength) {
        if (msgrcv(msgId, &recievedMessage, sizeof(recievedMessage) - sizeof(long), 1, 0) == -1) {
            perror("Error in msgrcv");
            exit(1);
        }

        if (recievedMessage.sum == ansArray[index]) {
            printf("Correct sum %d received for block %d\n", recievedMessage.sum, index + 1);
            index++;
            if (index < blockSize) { 
                sentMessage.security_value = security_values[index];
                sentMessage.decryption_key = decryption_keys[index];
            } else { 
                sentMessage.security_value = 0;
                sentMessage.decryption_key = 0;
            }
        } else {
            printf("Incorrect sum %d received for block %d\n",
                   recievedMessage.sum, index + 1);
            sentMessage.security_value = -1; 
            sentMessage.decryption_key = -1;
            error = 1;
        }

        if (msgsnd(msgId, &sentMessage, sizeof(sentMessage) - sizeof(long), 0) == -1) {
            perror("Error in msgsnd");
            exit(1);
        }
        if (error) break;
    }

    wait(NULL);
    gettimeofday(&stop, NULL);
    double time_taken = ((stop.tv_sec - start.tv_sec)) + ((stop.tv_usec - start.tv_usec) / 1e6);

    if (!error) {
        printf("All answers are correct\n");
        printf("Time taken by your solution to execute: %f seconds\n", time_taken);
          printf(
      "Please note that this number may fluctuate with server load, and won't "
      "be used for the final evaluation\n");
    }

    if (shmdt(shmPtr) == -1) { perror("Error in shmdt"); exit(1); }
    if (shmctl(shmid, IPC_RMID, NULL) == -1) { perror("Error in shmctl"); exit(1); }
    if (msgctl(msgId, IPC_RMID, NULL) == -1) { perror("Error in msgctl"); exit(1); }

    free(ansArray);
    return 0;
}