/******
Set the message shown on the display's header line.
******/
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "message.h"

/* Room to join the arguments before they are trimmed to what fits. */
#define INPUT_MAX 512

static void usage(FILE *out, const char *program)
{
	fprintf(out,
		"Usage: %s \"message\"   show a message on the display\n"
		"       %s --clear      go back to the default header\n"
		"       %s --show       print the message currently set\n"
		"\n"
		"Up to %d printable ASCII characters; anything longer or outside\n"
		"that range is trimmed, because the display cannot render it.\n"
		"A running display picks the change up within a couple of seconds.\n"
		"\n"
		"Message file: %s\n"
		"Override it with %s.\n",
		program, program, program,
		DISPLAY_MESSAGE_MAX - 1, display_message_path(), DISPLAY_MESSAGE_ENV);
}

/* Join the arguments with single spaces, so the message may be quoted or
   left as separate words. */
static void join_arguments(char *out, size_t length, int argc, char *argv[], int first)
{
	size_t used = 0;
	int index = 0;

	out[0] = '\0';
	for (index = first; index < argc; index++)
	{
		int written = snprintf(out + used, length - used, "%s%s",
		                       (used > 0) ? " " : "", argv[index]);
		if (written < 0 || (size_t)written >= length - used)
		{
			return; /* full; the message is trimmed to fit regardless */
		}
		used += (size_t)written;
	}
}

int main(int argc, char *argv[])
{
	const char *program = (argc > 0 && argv[0] != NULL) ? argv[0] : "display-cli";
	char input[INPUT_MAX] = {0};
	char message[DISPLAY_MESSAGE_MAX] = {0};

	if (argc < 2)
	{
		usage(stderr, program);
		return 2;
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
	{
		usage(stdout, program);
		return 0;
	}

	if (strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--show") == 0)
	{
		if (display_message_read(message, sizeof(message)))
		{
			printf("%s\n", message);
		}
		else
		{
			printf("No message set; the display is showing its default header.\n");
		}
		return 0;
	}

	if (strcmp(argv[1], "-c") == 0 || strcmp(argv[1], "--clear") == 0)
	{
		if (display_message_clear() != 0)
		{
			fprintf(stderr, "%s: cannot clear %s: %s\n",
			        program, display_message_path(), strerror(errno));
			return 1;
		}
		printf("Cleared. The display goes back to its default header.\n");
		return 0;
	}

	join_arguments(input, sizeof(input), argc, argv, 1);
	display_message_sanitize(message, sizeof(message), input);

	if (message[0] == '\0')
	{
		fprintf(stderr, "%s: nothing left to show after removing characters "
		                "the display cannot render\n", program);
		return 1;
	}

	if (display_message_write(message) != 0)
	{
		fprintf(stderr, "%s: cannot write %s: %s\n",
		        program, display_message_path(), strerror(errno));
		if (errno == EACCES || errno == EPERM)
		{
			fprintf(stderr, "The display runs as root, so its message file is "
			                "root-owned. Try: sudo %s \"%s\"\n", program, message);
		}
		return 1;
	}

	printf("Display header set to: %s\n", message);
	if (strcmp(input, message) != 0)
	{
		/* Flushed so the note follows the line above when stdout is a pipe
		   and therefore block buffered, rather than jumping ahead of it. */
		fflush(stdout);
		fprintf(stderr, "Note: trimmed to %d printable ASCII characters.\n",
		        (int)strlen(message));
	}
	return 0;
}
