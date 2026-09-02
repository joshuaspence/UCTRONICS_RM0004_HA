#include "message.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Enough for the message file's path and a ".tmp" suffix. */
#define MESSAGE_PATH_MAX 512

const char *display_message_path(void)
{
    const char *override = getenv(DISPLAY_MESSAGE_ENV);

    if (override != NULL && override[0] != '\0')
    {
        return override;
    }
    return DISPLAY_MESSAGE_PATH;
}

void display_message_sanitize(char *out, size_t length, const char *in)
{
    size_t count = 0;

    if (out == NULL || length == 0)
    {
        return;
    }
    if (in != NULL)
    {
        while (*in != '\0' && count + 1 < length)
        {
            unsigned char c = (unsigned char)*in++;

            /* lcd_write_char() indexes the font tables as (ch - 32), and
               they hold only the 95 printable ASCII glyphs. A byte outside
               that range reads outside the table: past the end where plain
               char is unsigned, as it is on ARM, and before the start
               where it is signed. Dropping those bytes here is what keeps
               arbitrary text from the CLI safe to render.

               This also strips the trailing newline when reading the file
               back, since it is below 32. */
            if (c >= 32 && c < 127)
            {
                out[count++] = (char)c;
            }
        }
    }
    out[count] = '\0';
}

int display_message_read(char *buffer, size_t length)
{
    FILE *fp = NULL;
    char line[256];

    if (buffer == NULL || length == 0)
    {
        return 0;
    }
    buffer[0] = '\0';

    fp = fopen(display_message_path(), "r");
    if (fp == NULL)
    {
        return 0;
    }
    if (fgets(line, sizeof(line), fp) == NULL)
    {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    /* Sanitised on the way out as well as on the way in, because nothing
       stops the file being edited by hand. */
    display_message_sanitize(buffer, length, line);
    return buffer[0] != '\0';
}

/*
* Create the directory holding the message file, if it is missing.
* Only the final component is created; its parent is expected to exist.
*/
static int ensure_parent_directory(const char *path)
{
    char directory[MESSAGE_PATH_MAX];
    char *slash = NULL;

    if (strlen(path) >= sizeof(directory))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(directory, path);
    slash = strrchr(directory, '/');
    if (slash == NULL || slash == directory)
    {
        return 0;
    }
    *slash = '\0';
    if (mkdir(directory, 0755) == 0 || errno == EEXIST)
    {
        return 0;
    }
    return -1;
}

int display_message_write(const char *text)
{
    const char *path = display_message_path();
    char temporary[MESSAGE_PATH_MAX];
    char clean[DISPLAY_MESSAGE_MAX];
    FILE *fp = NULL;

    display_message_sanitize(clean, sizeof(clean), text);

    if (ensure_parent_directory(path) != 0)
    {
        return -1;
    }
    if ((size_t)snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= sizeof(temporary))
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    fp = fopen(temporary, "w");
    if (fp == NULL)
    {
        return -1;
    }
    if (fprintf(fp, "%s\n", clean) < 0 || fclose(fp) != 0)
    {
        unlink(temporary);
        return -1;
    }

    /* rename() is atomic within a directory, so the daemon polling this
       file sees either the old message or the new one, never a partially
       written line. */
    if (rename(temporary, path) != 0)
    {
        unlink(temporary);
        return -1;
    }
    return 0;
}

int display_message_clear(void)
{
    if (unlink(display_message_path()) == 0 || errno == ENOENT)
    {
        return 0;
    }
    return -1;
}
