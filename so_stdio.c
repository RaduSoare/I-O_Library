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


struct _so_file {
	int file_descriptor;
	char *buffer;
	int bytes_in_buffer;
	int buffer_position;
	int last_operation;
	int reached_end;
	int computed_bytes;
	int error_flag;
	int child_pid;
};

	// Aloca structura SO_FILE
SO_FILE *init_so_file(int file_descriptor)
{
	SO_FILE *file_pointer;

	file_pointer = malloc(sizeof(SO_FILE));
	if (file_pointer == NULL) {
		perror("File pointer failed");
		return NULL;
	}

	file_pointer->file_descriptor = file_descriptor;
	file_pointer->buffer_position = 0; // Pozitia din buffer la care se afla cursorul
	file_pointer->bytes_in_buffer = 0; // Cati bytes se afla in bufferul curent
	file_pointer->computed_bytes = 0; // Cati bytes am citit in total
	file_pointer->last_operation = -1; // Retine daca ultima operatie a fost citire/scriere
	file_pointer->reached_end = 0; // 1 -> a ajuns la sfarsitul fisierului
	file_pointer->child_pid = 0; // pid-ul procesului creat cu fork
	file_pointer->error_flag = 0;// 1 -> pe parcursul programului a aparut o eroare 
	file_pointer->buffer = malloc(BUFSIZE * sizeof(char));
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

	// Deschide fisierul in modul specificat ca parametru
	if (strcmp(mode, "r") == 0)
		file_descriptor = open(pathname, O_RDONLY, ALL_PERMISIONS);

	if (strcmp(mode, "r+") == 0)
		file_descriptor = open(pathname, O_RDWR, ALL_PERMISIONS);

	if (strcmp(mode, "w") == 0)
		file_descriptor = open(pathname, O_CREAT | O_WRONLY | O_TRUNC, ALL_PERMISIONS);

	if (strcmp(mode, "w+") == 0)
		file_descriptor = open(pathname, O_CREAT | O_RDWR | O_TRUNC, ALL_PERMISIONS);

	if (strcmp(mode, "a") == 0)
		file_descriptor = open(pathname, O_CREAT | O_WRONLY | O_APPEND, ALL_PERMISIONS);

	if (strcmp(mode, "a+") == 0)
		file_descriptor = open(pathname, O_CREAT | O_RDWR | O_APPEND, ALL_PERMISIONS);

	if (file_descriptor < 0) {
		perror("Open failed");
		return NULL;
	}

	file_pointer = init_so_file(file_descriptor);

	return file_pointer;
}

// Elibereaza memoria unui SO_FILE si returneaza valoarea corespunzatoare
int deallocate_so_file(SO_FILE *fp, int ret)
{

	free(fp->buffer);
	free(fp);

	if (ret >= 0)
		return ret;

	return SO_EOF;
}

int so_fclose(SO_FILE *stream)
{
	int rc;
	int fd;

	// Retin fd-ul pentru a putea da close dupa eliberarea structurii
	fd = stream->file_descriptor;

	// Scrie in fisier in caz ca au mai ramas date in buffer
	rc = so_fflush(stream);
	if (rc < 0) {
		perror("fflush failed.");
		return deallocate_so_file(stream, SO_EOF);
	}

	// Inchide fisierul
	rc = close(fd);
	if (rc < 0) {
		perror("Close failed.");
		return deallocate_so_file(stream, SO_EOF);
	}

	// Elibereaza memoria
	return deallocate_so_file(stream, 0);

	}

int so_fgetc(SO_FILE *stream)
{
	int result;

	// Daca bufferul e gol sau s-a ajuns la finalul bufferului se face syscall
	if (stream->bytes_in_buffer == 0 || stream->buffer_position == stream->bytes_in_buffer) {

		// Cand ajung la finalul bufferului, mut cursorul pentru cati bytes am reusit sa citesc
		if (stream->buffer_position == stream->bytes_in_buffer)
			stream->computed_bytes += stream->bytes_in_buffer;

		// Curat si reinitializez datele bufferului inainte de citire
		stream->buffer_position = 0;
		memset(stream->buffer, 0, BUFSIZE);
		stream->bytes_in_buffer = read(stream->file_descriptor, stream->buffer, BUFSIZE);

		// Marchez ca am ajuns la finalul bufferului
		if (stream->bytes_in_buffer <= 0) {
			stream->reached_end = 1;
			stream->error_flag = 1;
			return SO_EOF;
		}

	}
	// Retin caracterul curent pentru a-l putea returna
	result = (unsigned char)(stream->buffer[stream->buffer_position]);

	// Mut cursorul pe urmatorul caracter de citit
	stream->buffer_position++;
	// Marcheaza citirea ca ultima operatie efectuata
	stream->last_operation = READ_OP;

	return result;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int char_read = 0;

	for (size_t i = 0; i < nmemb * size; i++) {
		// Citeste un caracter
		char_read = so_fgetc(stream);
		if (char_read == SO_EOF) {
			stream->error_flag = 1;
			// In caz de eroare returneaza numarul de elemente citite pana la eroare
			return i;
		}
		*(((char *)ptr) + i) = (char)char_read;
	}
	// Daca s-a ajuns aici inseamna ca s-au putut citi toate elementele
	return nmemb;
}

int so_fputc(int c, SO_FILE *stream)
{
	int bytes_written = 0, rc = 0;

	// Marcheaza scrierea ca ultima operatie efectuata
	stream->last_operation = WRITE_OP;

	// Daca bufferul a fost umplut, se face fflush in fisier
	if (stream->buffer_position >= BUFSIZE) {

		rc = so_fflush(stream);
		if (rc < 0) {
			perror("Fflush failed.");
			stream->error_flag = 1;
			return SO_EOF;
		}
	}
	// Pune in fisier caracterul
	stream->buffer[stream->buffer_position] = (unsigned char)c;
	// Muta cursorul si updateaza numarul de bytes din buffer
	stream->buffer_position++;
	stream->bytes_in_buffer++;

	return c;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{

	int byte_written, result;

	for (size_t i = 0; i < size * nmemb; i++) {
		// Citeste un caracter
		byte_written = so_fputc(*((unsigned char *)ptr + i), stream);

		if (byte_written == SO_EOF) {
			perror("Fputc failed.");
			stream->error_flag = 1;
			return i;
		}
	}
	// Daca s-a ajuns aici inseamna ca s-au putut scrie toate elementele
	return nmemb;
	}

int so_fileno(SO_FILE *stream)
{
	return stream->file_descriptor;
}

int so_fflush(SO_FILE *stream)
{
	int rc;
	ssize_t bytes_written = 0, bytes_written_now = 0;

	// Daca ultima operatie a fost de scriere, scrie in fisier caracterele aflate in buffer
	if (stream->last_operation == WRITE_OP) {

		// Scrie elemente intr-o bucla deoarece write poate sa nu scrie BUFSIZE de fiecare data
		while (bytes_written < stream->bytes_in_buffer) {
			bytes_written_now = write(stream->file_descriptor, stream->buffer + bytes_written,
										stream->bytes_in_buffer - bytes_written);

			// Retine cati bytes au fost scrisi in cadrul iteratiei
			bytes_written += bytes_written_now;

			if (bytes_written_now <= 0) {
				perror("Fflush error.");
				stream->error_flag = 1;
				return SO_EOF;
			}
		}
		// Retine ca cititi bytii din buffer care au fost scrisi in fisier
		stream->computed_bytes += bytes_written;

	}
	// Curata si reinitilizeaza bufferul
	memset(stream->buffer, 0, BUFSIZE);
	stream->buffer_position = 0;
	stream->bytes_in_buffer = 0;
	return 0;

}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	int new_pos, rc;

	// Invalideaza bufferul daca ultima operatie a fost de citire
	if (stream->last_operation == READ_OP) {
		memset(stream->buffer, 0, BUFSIZE);
		stream->bytes_in_buffer = 0;
		stream->buffer_position = 0;
	} else if (stream->last_operation == WRITE_OP) {
		// Scrie in fisier inainte de fseek
		rc = so_fflush(stream);
		if (rc < 0) {
			perror("Fflush failed.");
			stream->error_flag = 1;
			return SO_EOF;
		}

	}
	// Muta cursorul in functie de parametrii
	new_pos = lseek(stream->file_descriptor, offset, whence);
	if (new_pos < 0) {
		perror("Lseek failed.");
		stream->error_flag = 1;
		return SO_EOF;
	}

	stream->computed_bytes = new_pos;
	return 0;

}

long so_ftell(SO_FILE *stream)
{
	/*
	 *	Calculeaza pozitia curenta a cursorului in functie de cati byti s-au citit
	 *	in total + cati byti se afla in buffer in momentul respectiv
	 */
	return stream->buffer_position + stream->computed_bytes;
}

int so_feof(SO_FILE *stream)
{
	// Verifica flag-ul din cadrul structurii
	return stream->reached_end == 1 ? 1 : 0;
}

int so_ferror(SO_FILE *stream)
{
	// Verifica flagul pentru erori
	return stream->error_flag == 1 ? 1 : 0;
}

/* Functii procese */

SO_FILE *so_popen(const char *command, const char *type)
{

	int rc, fd;
	SO_FILE *fp;
	SO_FILE *ret_val = NULL;
	int filedes[2];
	char **argument_list;

	// Aloca array-ul de argumente al comenzii
	argument_list = calloc(MAX_ARGUMENTS, sizeof(char *));
	if (argument_list == NULL) {
		perror("Malloc failed");
		return NULL;
	}

	for (int i = 0; i < MAX_ARGUMENTS; i++) {
		argument_list[i] = calloc(WORD_LEN, sizeof(char));
		if (argument_list[i] == NULL) {
			for (int j = 0; j < i; j++)
				free(argument_list[j]);
			return NULL;
		}
	}

	// Populeaza array-ul cu argumentele
	strncpy(argument_list[0], "sh", 2);
	strncpy(argument_list[1], "-c", 2);
	strncpy(argument_list[2], command, strlen(command));

	// Apel open doar pentru a obtine un file descriptor liber
	fd = open(" ", O_CREAT, ALL_PERMISIONS);
	// Aloca o structura SO_FILE
	fp = init_so_file(fd);

	//Creeaza pipe-ul
	rc = pipe(filedes);
	if (rc < 0) {
		perror("Pipe failed");
		fp->error_flag = 1;
		return NULL;
	}

	fp->child_pid = fork();
	switch (fp->child_pid) {
	case -1:
		perror("fork failed");
		fp->error_flag = 1;
		break;
	case 0:
	// Procesul copil
		if (type[0] == 'r') {
			// Vice-versa w
			// Inchide capatul nefolosit al pipe-ului
			rc = close(filedes[READ_OP]);
			if (rc < 0) {
				perror("Close failed");
				return NULL;
			}
			// Redirecteaza iesirea standard catre capatul de scriere al pipe-ului
			rc = dup2(filedes[WRITE_OP], STDOUT_FILENO);
			if (rc < 0) {
				perror("Dup2 failed");
				return NULL;
			}
			// Inchide si capatul de scriere
			rc = close(filedes[WRITE_OP]);
			if (rc < 0) {
				perror("Close failed");
				return NULL;
			}

		} else if (type[0] == 'w') {
			// Inchide capatul nefolosit al pipe-ului
			rc = close(filedes[WRITE_OP]);
			if (rc < 0) {
				perror("Close failed");
				return NULL;
			}
			/*
			 * Se muta citirea din inputul standard catre capatul de citire al pipe-ului
			 * Datele ce se doreau scrise in structura SO_FILE sunt citite acum de
			 * procesul copil
			 */
			rc = dup2(filedes[READ_OP], STDIN_FILENO);
			if (rc < 0) {
				perror("Dup2 failed");
				return NULL;
			}
			rc = close(filedes[READ_OP]);
			if (rc < 0) {
				perror("Close failed");
				return NULL;
			}
		}

		/*
		 * Se inlocuieste imaginea procesului creat cu imaginea
		 * procesului creat cu fork, cu imaginea procesului creat de 'command'
		 */
		rc = execvp(argument_list[0], argument_list);
		if (rc < 0) {
			perror("Execvp failed");
			free(fp->buffer);
			free(fp);

			for (int i = 0; i < MAX_ARGUMENTS; i++)
				free(argument_list[i]);
			free(argument_list);
			return NULL;
		}
		break;
	default:
		// Procesul parinte
			// Vice-versa w
			if (strcmp(type, "r") == 0) {
				rc = close(filedes[WRITE_OP]);
				if (rc < 0) {
					perror("Close failed");
					return NULL;
				}
				fp->file_descriptor = filedes[READ_OP];

			} else if (strcmp(type, "w") == 0) {
				// Inchide capatul nefolosit al pipe-ului
				rc = close(filedes[READ_OP]);
				if (rc < 0) {
					perror("Close failed");
					return NULL;
				}
				/*
				 * Operatiile de scriere ce se doreau a fi facute pe SO_FILE sunt redirectate
				 * catre capatul de scriere al pipe-ului
				 */
				fp->file_descriptor = filedes[WRITE_OP];
			}

			ret_val = fp;
	}


	for (int i = 0; i < MAX_ARGUMENTS; i++)
		free(argument_list[i]);
	free(argument_list);

	// Dezaloca SO_FILE in caz de eroare
	if (!ret_val) {
		free(fp->buffer);
		free(fp);
	}

return ret_val;
}

int so_pclose(SO_FILE *stream)
{
	int wait_rc;
	int status = 0;
	int rc;

	// Goleste bufferul inainte de a inchide fisierul
	rc = so_fflush(stream);
	if (rc < 0) {
		perror("fflush failed.");
		return deallocate_so_file(stream, SO_EOF);
	}
	rc = close(stream->file_descriptor);
	if (rc < 0) {
		perror("Close failed");
		return deallocate_so_file(stream, SO_EOF);
	}

	// Procesul parinte asteapta terminarea procesului copil
	wait_rc = waitpid(stream->child_pid, &status, 0);
	if (wait_rc < 0) {
		perror("Waitpid failed");
		return deallocate_so_file(stream, SO_EOF);
	}

	if (!WIFEXITED(status))
		deallocate_so_file(stream, SO_EOF);

	return deallocate_so_file(stream, status);

}
