#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

int main(int argc, char const *argv[])
{
    openlog(NULL,0,LOG_USER);

    // The 0th argument is the program itself
    if (argc-1 != 2)
    {
        syslog(LOG_ERR, "Error: 2 arguments are required. Number of arguments given %d", argc-1);
        return EXIT_FAILURE;
    }

    syslog(LOG_DEBUG, "Writing \"%s\" to %s!", argv[2], argv[1]);

    const char* filename = argv[1];
    const char* text = argv[2];
    FILE* file = fopen(filename, "w");

    int length_text = strlen(text);

    if(file == NULL)
    {
        syslog(LOG_ERR, "File %s could not be modified/created! Errno: %s", argv[1], strerror(errno));
        return EXIT_FAILURE;
    } else {
        int ret = fprintf(file, "%s" ,text);
        fclose(file);
        if(ret < length_text)
        {
            syslog(LOG_ERR, "Writing the file %s failed! Errno: %s", argv[1], strerror(errno));
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
