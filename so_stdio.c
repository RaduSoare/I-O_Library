#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "so_stdio.h"

#define BUFSIZE 4096
#define ALL_PERMISIONS 0644
#define READ_OP 0
#define WRITE_OP 1
#define MAX_ARGUMENTS 10
#define WORD_LEN 50

int global_error_flag = 0;

struct _so_file {
    int file_descriptor;
    char *buffer;
    int bytes_in_buffer;
    int buffer_position;
    int last_operation;
    int reached_end;
    int bytes_read;
    int error_flag;
    int pid;
};

SO_FILE* init_so_file(int file_descriptor)
{
    SO_FILE *file_pointer;
    file_pointer = malloc(sizeof(SO_FILE));
    if (file_pointer == NULL) {
        perror("File pointer failed");
        return NULL;
    }

    file_pointer->file_descriptor = file_descriptor;
    file_pointer->buffer_position = 0; // Pozitia din buffer la care se afla cursorul
    file_pointer->buffer = malloc(BUFSIZE * sizeof(char));
    file_pointer->bytes_in_buffer = 0; // Cati bytes se afla in bufferul curent
    file_pointer->bytes_read = 0; // Cati bytes am citit in total
    file_pointer->last_operation = -1;
    file_pointer->reached_end = 0; // 1 -> a ajuns la sfarsitul fisierului
    file_pointer->pid = 0;
    if (file_pointer->buffer == NULL) {
        perror("Buffer allocation failed");
        free(file_pointer);
        return NULL;
    }

    return file_pointer;
}

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
    SO_FILE *file_pointer;
    int file_descriptor = -1;

    if (strcmp(mode, "r") == 0) {
        file_descriptor = open(pathname, O_RDONLY, ALL_PERMISIONS);
    }
    if (strcmp(mode, "r+") == 0) {
        file_descriptor = open(pathname, O_RDWR, ALL_PERMISIONS);
    }
    if (strcmp(mode, "w") == 0) {
        file_descriptor = open(pathname, O_CREAT | O_WRONLY | O_TRUNC, ALL_PERMISIONS);
    }
    if (strcmp(mode, "w+") == 0) {
        file_descriptor = open(pathname, O_CREAT | O_RDWR | O_TRUNC, ALL_PERMISIONS);
    }
    if (strcmp(mode, "a") == 0) {
        file_descriptor = open(pathname, O_CREAT | O_WRONLY | O_APPEND, ALL_PERMISIONS);
    }
    if (strcmp(mode, "a+") == 0) {
        file_descriptor = open(pathname, O_CREAT | O_RDWR | O_APPEND, ALL_PERMISIONS);
    }

    if (file_descriptor < 0) {
        perror("Open failed");
        return NULL;
    }

    file_pointer = init_so_file(file_descriptor);

    return file_pointer;
}

int so_fclose(SO_FILE *stream)
{
    int rc;
    int fd;

    fd = stream->file_descriptor;

    rc = so_fflush(stream);
    if (rc < 0) {
        perror("fflush failed.");
        goto return_error;
    }
    rc = close(fd);
    
    
    if (rc < 0) {
        perror("Close failed.");
        goto return_error;
    }

    goto return_success;

return_error:
    global_error_flag = 1;
    free(stream->buffer);
    free(stream);
    return SO_EOF;

return_success:
    free(stream->buffer);
    free(stream);
    return 0;
    
    
    
}


int so_fgetc(SO_FILE *stream)
{
    int result;
    
    // Daca bufferul e gol sau s-a ajuns la finalul bufferului se face syscall
    if (stream->bytes_in_buffer == 0 || stream->buffer_position == stream->bytes_in_buffer){
        
        // Cand ajung la finalul bufferului, mut cursorul pentru cati bytes am reusit sa citesc
        if (stream->buffer_position == stream->bytes_in_buffer) {
            stream->bytes_read += stream->bytes_in_buffer;
        }
        stream->buffer_position = 0;
        memset(stream->buffer, 0, BUFSIZE);
        stream->bytes_in_buffer = read(stream->file_descriptor, stream->buffer, BUFSIZE);
        
        if (stream->bytes_in_buffer <= 0) {
            stream->reached_end = 1;
            global_error_flag = 1;
            return SO_EOF;
        }
        
    }
    result = (unsigned char)(stream->buffer[stream->buffer_position]);
       
    stream->buffer_position++;
    // Marcheaza citirea ca ultima operatie efectuata
    stream->last_operation = READ_OP;
    return result;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
    int char_read = 0;
    
    for (size_t i = 0; i < nmemb * size; i++)
    {
        char_read = so_fgetc(stream);  
        if (char_read == SO_EOF) {
             global_error_flag = 1;
            return i;
        }
        *(((char *)ptr) + i) = (char)char_read; 
    }
    
    return nmemb;
}

int so_fputc(int c, SO_FILE *stream) {
    int bytes_written = 0, rc = 0;
    
    stream->last_operation = WRITE_OP;
    if (stream->buffer_position >= BUFSIZE) {
        
        rc = so_fflush(stream);
        if (rc < 0) {
            perror("Fflush failed.");
            global_error_flag = 1;
            return SO_EOF;
        }
    }
    stream->buffer[stream->buffer_position] = (unsigned char)c;
    stream->buffer_position++;
    stream->bytes_in_buffer++;
    
    
    return c;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
    
    int byte_written, result;

    for (size_t i = 0; i < size * nmemb; i++) {
        byte_written = so_fputc(*((unsigned char *)ptr + i), stream);
        
        if (byte_written == SO_EOF) {
            perror("Fputc failed.");
            global_error_flag = 1;
            return i;
        }
    }
    
    return nmemb;
}

int so_fileno(SO_FILE *stream) {
    return stream->file_descriptor;
}

int so_fflush(SO_FILE *stream)
{
    int rc;
    ssize_t bytes_written = 0, bytes_written_now = 0;

    if (stream->last_operation == WRITE_OP) {
       
        
        while (bytes_written < stream->bytes_in_buffer) {
            bytes_written_now = write(stream->file_descriptor, stream->buffer + bytes_written,
                                        stream->bytes_in_buffer - bytes_written);

            bytes_written += bytes_written_now;
          
            if (bytes_written_now <= 0) {
                perror("Fflush error.");
                global_error_flag = 1;
                return SO_EOF;
            }
        }
        //printf("%d\n", bytes_written);
        // Retine ca cititi bytii din buffer care au fost scrisi in fisier
        stream->bytes_read += bytes_written;
        
    }
    memset(stream->buffer, 0, BUFSIZE);
    stream->buffer_position = 0;
    stream->bytes_in_buffer = 0;
    return 0;
    
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
    int new_pos, rc;

    // Invalideaza bufferul
    if (stream->last_operation == READ_OP) {
        memset(stream->buffer, 0, BUFSIZE);
        stream->bytes_in_buffer = 0;
        stream->buffer_position = 0;
    } else if (stream->last_operation == WRITE_OP){
        // Scrie in fisier inainte de fseek
        rc = so_fflush(stream);
        if (rc < 0) {
            perror("Fflush failed.");
            global_error_flag = 1;
            return SO_EOF;
        }

    }
    new_pos = lseek(stream->file_descriptor, offset, whence);
    if (new_pos < 0) {
        perror("Lseek failed.");
        global_error_flag = 1;
        return SO_EOF;
    }
    
    stream->bytes_read = new_pos;
    return 0;

    
}

long so_ftell(SO_FILE *stream)
{
    return stream->buffer_position + stream->bytes_read;
}

int so_feof(SO_FILE *stream)
{
    return stream->reached_end == 1 ? 1 : 0;
}

int so_ferror(SO_FILE *stream) {
    
    return global_error_flag == 1 ? 1 : 0;
}



char **parse_command(char *command, int *count)
{
    
    char **argument_list = (char **)malloc(MAX_ARGUMENTS * sizeof(char *));
    for (int i = 0; i < MAX_ARGUMENTS; i++) {
        argument_list[i] = malloc(WORD_LEN * sizeof(char));
    }
    char *token;
    token = strtok(command, " ");
   
   /* walk through other tokens */
    while( token != NULL ) {
        strcpy(argument_list[(*count)++], token);
        token = strtok(NULL, " ");
    }
    // Elibereaza memoria inainte de a face NULL ca sa nu se piarda
    free(argument_list[(*count)]);
    argument_list[(*count)++] = NULL;
    // for (int i = 0; i < (*count) - 1; i++) {
    //     printf("%s\n", argument_list[i]);
    // }
    return argument_list;
}


SO_FILE *so_popen(const char *command, const char *type)
{

    char* precomm[] = {"sh", "-c"};
    pid_t pid;
    int rc, fd;
    SO_FILE *fp;
    SO_FILE *ret_val = NULL;
    int filedes[2];
    int count = 0;

    char **arglist = calloc(MAX_ARGUMENTS , sizeof(char*));
    for(int i = 0; i < MAX_ARGUMENTS; i++)
    {
        arglist[i] = calloc(WORD_LEN, sizeof(char));
    }

    strncpy(arglist[0], "sh", 2);
    strncpy(arglist[1], "-c", 2);
    strncpy(arglist[2], command, strlen(command));

    fp = init_so_file(10);

    rc = pipe(filedes);
    if (rc < 0) {
        perror("Pipe failed");
        global_error_flag = 1;
    } else {
        fp->pid = fork();
        switch (fp->pid)
        {
            case -1:
                perror("fork failed");
                global_error_flag = 1;
                break;
            case 0:
            /* Child process */
                if (type[0] == 'r')
                {
                    close(filedes[READ_OP]);
                    /* stdout and pipe[write] are now pointing to same underlying stream, stdout */
                    rc = dup2(filedes[WRITE_OP], STDOUT_FILENO);
                    /* However, they have different values and are different fds, we can close one of them */
                    close(filedes[WRITE_OP]);

                } else if (type[0] == 'w') {
                    close(filedes[WRITE_OP]);
                    rc = dup2(filedes[READ_OP], STDIN_FILENO);
                    close(filedes[READ_OP]);
                }
                
                rc = execvp(arglist[0], arglist); /* Nothing is executed after execvp is reached, unless execvp fails -> if check is useless */
                if (rc < 0) {
                    perror("Execvp failed");
                    free(fp->buffer);
                    free(fp);

                    for (int i = 0; i < MAX_ARGUMENTS; i++) {
                        free(arglist[i]);
                    }
                    free(arglist);
                    exit(-1); /* Execve should never return, if it does we should also close child process */
                }
                break;
            default:
            /* Parent */
                if (strcmp(type, "r") == 0) {
                    close(filedes[WRITE_OP]);

                    /* fp->fd and pipe[read] now point to same underlying stream */
                    fp->file_descriptor = filedes[READ_OP];
                    /* However, they are 2 different descriptors, so one of them needs to be closed */
                    // close(filedes[READ_OP]);

                } else if (strcmp(type, "w") == 0) {
                    /* Vice-versa to "r" branch */
                    close(filedes[READ_OP]);
                    fp->file_descriptor = filedes[WRITE_OP];
                    // close(filedes[WRITE_OP]);
                }

                ret_val = fp;
        }
    }

    for (int i = 0; i < MAX_ARGUMENTS; i++) {
        free(arglist[i]);
    }
    free(arglist);

    if (!ret_val)
    {
        /* Structure which will be returned is invalid, deallocate initially allocated one */
        free(fp->buffer);
        free(fp);
    }

    return ret_val;
}

int so_pclose(SO_FILE *stream)
{
    int wait_rc;
    int status;

    int rc = so_fflush(stream);
    if (rc < 0) {
        perror("fflush failed.");
        goto return_error;
    }
    rc = close(stream->file_descriptor);


    wait_rc = waitpid(stream->pid, &status, 0);
    if (wait_rc < 0) {
        goto return_error;
    }

    goto return_success;

return_success:
    free(stream->buffer);
    free(stream);

    return status;

return_error:
    free(stream->buffer);
    free(stream);
    global_error_flag = 1;
    return -1;

}
