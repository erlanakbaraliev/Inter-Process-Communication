#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h> 
#include <sys/msg.h> 


struct msgSize {
  long mtype;
  char nVoters;
};

struct idElig {
  long mtype;
  int ID;
  char elig[1024];
  int votes;
};

void handle(int signumber);
void generateID(int nVoters, int fd[], int* voterID);
void readID(int fd[], int nVoters, int* voterIDs);
void readID_Elig(char pipepath[]);
void writeId_Elig_nPipe(char pipepath[], int voterIDs[], int nVoters);
void readID_Elig_nPipe(int nPipe, struct idElig* idEligs);
void sndChoicesMqueue(int mqueue, struct idElig idEligs[], int nVoters);
void readChoicesMqueue(int mqueue);

int main(int argc, char* argv[]){
  signal(SIGTERM, handle);
  signal(SIGUSR1, handle);

  int nVoters;
  int status;

  int fd[2];
  if(pipe(fd) < 0) {
    perror("Error while creating pipe\n");
    return 1;
  }


  char pipepath[] = "/tmp/erlanPath";
  unlink(pipepath);

  int npID = mkfifo(pipepath, S_IREAD | S_IWRITE);
  if(npID < 0) {
    perror("Error in mkfifo\n");
    return 1;
  }

  int nP;

  key_t key = ftok(argv[0], 12131);
  int mqueue = msgget(key, 0600 | IPC_CREAT);  
  if(mqueue < 0) {
      perror("Error in msgget\n");
      return 1;
  }


  int child1, child2;
  child1 = fork();

  if(child1 < 0) {
    perror("Error in child1\n");
    return 1;
  } 
  else if (child1 == 0) { //Checking child
    printf("Wait 3 secs (checking child)\n");
    sleep(3);
    kill(getppid(), SIGTERM); //checking child ready

    pause();

    close(fd[1]);

    int nVoters;
    read(fd[0], &nVoters, sizeof(int));

    int voterIDs[nVoters];

    readID(fd, nVoters, &voterIDs[0]);
    printf("Checking child finished\n");

    writeId_Elig_nPipe(pipepath, voterIDs, nVoters);
  }
  else { //President
    child2 = fork();

    if(child2 < 0) {
      perror("Error in child2\n");
    }
    else if(child2 == 0) { //Sealing child 
      printf("Wait 2 secs (sealing child)\n");
      sleep(2);
      kill(getppid(), SIGUSR1); //checking child ready

      int nPipe = open(pipepath, O_RDONLY);

      int nVoters;
      read(nPipe, &nVoters, sizeof(int));

      struct idElig idEligs[nVoters];      

      readID_Elig_nPipe(nPipe, idEligs);

      sndChoicesMqueue(mqueue, idEligs, nVoters);

      printf("Sealing child finished\n"); 
    }
    else { //President
      pause();
      printf("Checking child is ready\n");
      pause();
      printf("Sealing child is ready\n");

      printf("Enter the number of voters: ");
      scanf("%d", &nVoters);

      int voterID[nVoters];

      generateID(nVoters, fd, &voterID[0]);

      kill(child1, SIGTERM);

      waitpid(child2, &status, 0);

      readChoicesMqueue(mqueue);
      printf("Main finished\n");
    }
  }

  return 0;
}


void handle(int signumber) {
  // printf("Signal %d\n", signumber);
}

void generateID(int nVoters, int fd[], int* voterID) {
  close(fd[0]);

  write(fd[1], &nVoters, sizeof(int));

  int n; 
  srand(time(NULL));

  for (int i = 0; i < nVoters; i++) {
    n = rand() % 1000;
    voterID[i] = n;
    write(fd[1], &n, sizeof(int));
  }

  close(fd[1]);
}

void readID(int fd[], int nVoters, int* voterIDs) {
  int readOut;

  for (int i = 0; i < nVoters; i++) {
    int c = read(fd[0], &readOut, sizeof(int));
    voterIDs[i] = readOut;

    if (c < 0) { continue; }

    printf("ID %d\n", readOut);
  }
  
  close(fd[0]);
}


void writeId_Elig_nPipe(char pipepath[], int voterIDs[], int nVoters) {
  int nPipe = open(pipepath, O_WRONLY);

  int twentyPercent = round(nVoters * 0.2);

  write(nPipe, &nVoters, sizeof(int));

  for(int i=0; i<nVoters; i++) {
    write(nPipe, &voterIDs[i], sizeof(int));
 
    if(i < twentyPercent) {
        char elig[] = "cannot vote";
        int l = sizeof(elig);
        write(nPipe, &l, sizeof(int));

        write(nPipe, elig, l);
    } else {
        char elig[] = "can vote";
        int l = sizeof(elig);
        write(nPipe, &l, sizeof(int));

        write(nPipe, elig, l);
    }
  }

  close(nPipe);
}

void readID_Elig_nPipe(int nPipe, struct idElig* idEligs){
  int id;
  int length;

  int i=0;
  while(read(nPipe, &id, sizeof(int)) > 0) {
    read(nPipe, &length, sizeof(int));
  
    char elig[length];
    read(nPipe, &elig, length);

    printf("ID %d - %s\n", id, elig);

    idEligs[i].ID = id;
    strcpy(idEligs[i++].elig, elig);
  }

  close(nPipe);
}

void sndChoicesMqueue(int mqueue, struct idElig idEligs[], int nVoters){
  struct msgSize msgS = {1, nVoters};

  msgsnd(mqueue, &msgS, sizeof(struct msgSize) - sizeof(long), 0);

  for(int i=0; i<nVoters; i++) {
    struct idElig tmp;
    tmp.mtype = 1;
    tmp.ID = idEligs[i].ID;
    strcpy(tmp.elig, idEligs[i].elig);

    if(strlen(tmp.elig) <= 8) {
      tmp.votes = (rand() % 6) + 1;
    }
    else {
      tmp.votes = 0;
    }


    if(msgsnd(mqueue, &tmp, sizeof(struct idElig) - sizeof(long), 0) < 0) {
      perror("Error in msgsnd\n");
      return;
    }
  }
}

void readChoicesMqueue(int mqueue) {
  struct msgSize msgS;
  msgrcv(mqueue, &msgS, sizeof(struct msgSize) - sizeof(long), 1, 0);

  int nVoters = msgS.nVoters;

  struct idElig m;
  srand(time(NULL));

  for(int i=0; i<nVoters; i++) {
    if(msgrcv(mqueue, &m, sizeof(struct idElig) - sizeof(long), 1, 0) < 0) {
      perror("Error in msgrcv readChoicesMqueue\n");
      return;
    }
    printf("%d, %d, %s, %d\n", m.mtype, m.ID, m.elig, m.votes);
  }
}