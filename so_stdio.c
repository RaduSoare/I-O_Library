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
    long file_position;
    int last_operation;
    int reached_end;
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
    DIE(file_pointer < 0, "SO_FILE creation failed.");

    file_pointer->file_descriptor = file_descriptor;
    file_pointer->file_position = 0;
    file_pointer->buffer = malloc(BUFSIZE * sizeof(char));
    file_pointer->bytes_in_buffer = 0;
    file_pointer->last_operation = -1;
    file_pointer->reached_end = 0; // 1 -> a ajuns la sfarsitul fisierului
    if (file_pointer->buffer < 0) {
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
    int bytes_read, result;
    
    // Daca bufferul e gol sau s-a ajuns la finalul bufferului se face syscall
    if (stream->bytes_in_buffer == 0 || stream->file_position == stream->bytes_in_buffer){
        
        stream->file_position = 0;
        memset(stream->buffer, 0, BUFSIZE);
        stream->bytes_in_buffer = read(stream->file_descriptor, stream->buffer, BUFSIZE);

        if (stream->bytes_in_buffer <= 0) {
            //printf("aici\n");
            stream->reached_end = 1;
            return SO_EOF;
        }
        
    }
    result = (unsigned char)(stream->buffer[stream->file_position]);
       
    stream->file_position++;
    // Marcheaza citirea ca ultima operatie efectuata
    stream->last_operation = READ_OP;
    return result;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
    int char_read = 0;
   // printf("%ld %ld\n", size, nmemb);
    for (size_t i = 0; i < nmemb * size; i++)
    {
        char_read = so_fgetc(stream);  
        if (char_read == SO_EOF) {
            return i;
        }
        *(((char *)ptr) + i) = (char)char_read; 
    }
    
    return nmemb;
}

int so_fputc(int c, SO_FILE *stream) {
    int bytes_written = 0, rc = 0;

    if (stream->file_position >= BUFSIZE) {
        rc = so_fflush(stream);
        if (rc < 0) {
            perror("Fflush failed.");
            return SO_EOF;
        }
    }
    stream->buffer[stream->file_position] = c;
    stream->file_position++;
    stream->bytes_in_buffer++;
    stream->last_operation = WRITE_OP;
    //printf("%c\n", c);
    return c;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
    
    int byte_written, result;
    for (size_t i = 0; i < size * nmemb; i++) {
        byte_written = so_fputc(*((unsigned char *)ptr + i), stream);
        
        if (byte_written == SO_EOF) {
            perror("Fputc failed.");
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

    if (stream->last_operation == WRITE_OP) {
        rc = write(stream->file_descriptor, stream->buffer, stream->bytes_in_buffer);
        if (rc < 0) {
            perror("Fflush error.");
            return SO_EOF;
        }
    }
    memset(stream->buffer, 0, BUFSIZE);
    stream->file_position = 0;
    stream->bytes_in_buffer = 0;
    return 0;
    
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
    return 0;
}

long so_ftell(SO_FILE *stream)
{
    return stream->file_position;
}

int so_feof(SO_FILE *stream)
{
    return stream->reached_end == 1 ? 1 : 0;
}

int so_ferror(SO_FILE *stream) {
    return 0;
}

SO_FILE *so_popen(const char *command, const char *type)
{
    return NULL;
}

int so_pclose(SO_FILE *stream)
{
    return 0;
}
