#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "so_stdio.h"
#include "utils/utils.h"

#define BUFSIZE 4096
#define ALL_PERMISIONS 0644
#define READ_OP 1
#define WRITE_OP 2

struct _so_file {
    int file_descriptor;
    char *buffer;
    int bytes_in_buffer;
    long buffer_position;
    int last_operation;
    int reached_end;
    int bytes_read;
    int error_flag;
};

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
        file_descriptor = open(pathname, O_CREAT | O_RDONLY | O_APPEND, ALL_PERMISIONS);
    }

    if (file_descriptor < 0) {
        perror("Open failed");
        return NULL;
    }

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
    file_pointer->error_flag = 0; // 1-> s-a intalnit o eroare
    if (file_pointer->buffer == NULL) {
        perror("Buffer allocation failed");
        free(file_pointer);
        return NULL;
    }
   

    return file_pointer;
}

int so_fclose(SO_FILE *stream)
{
    int rc;
    
    rc = so_fflush(stream);
    if (rc < 0) {
        perror("fflush failed.");
        stream->error_flag = 1;
        return SO_EOF;
    }
    rc = close(stream->file_descriptor);
    
    free(stream->buffer);
    free(stream);
    if (rc < 0) {
        perror("Close failed.");
        
        return SO_EOF;
    }
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
            stream->error_flag = 1;
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
             stream->error_flag = 1;
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
            stream->error_flag = 1;
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
            stream->error_flag = 1;
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
    size_t bytes_written = 0, bytes_written_now = 0;

    if (stream->last_operation == WRITE_OP) {
        while (bytes_written < stream->bytes_in_buffer) {
            bytes_written_now = write(stream->file_descriptor, stream->buffer + bytes_written,
                                        stream->bytes_in_buffer - bytes_written);
            bytes_written += bytes_written_now;

            if (bytes_written_now <= 0) {
                perror("Fflush error.");
                 stream->error_flag = 1;
                return SO_EOF;
            }
        }
        // Retine ca cititi bytes din buffer care au fost scrisi in fisier
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
            stream->error_flag = 1;
            return SO_EOF;
        }

    }
    new_pos = lseek(stream->file_descriptor, offset, whence);
    if (new_pos < 0) {
        perror("Lseek failed.");
        stream->error_flag = 1;
        return SO_EOF;
    }
    
    stream->bytes_read = new_pos;
    return 0;

    
}

long so_ftell(SO_FILE *stream)
{
    //printf("%d %d %d\n", stream->buffer_position, stream->bytes_in_buffer, stream->bytes_read);
    return stream->buffer_position + stream->bytes_read;
}

int so_feof(SO_FILE *stream)
{
    return stream->reached_end == 1 ? 1 : 0;
}

int so_ferror(SO_FILE *stream) {
    return stream->error_flag == 1 ? 1 : 0;
}

SO_FILE *so_popen(const char *command, const char *type)
{
    return NULL;
}

int so_pclose(SO_FILE *stream)
{
    return 0;
}
