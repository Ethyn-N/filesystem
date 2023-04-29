#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>

#define NUM_BLOCKS 65536
#define BLOCK_SIZE 1024
#define MAX_BLOCKS_PER_FILE 1024
#define MAX_FILE_SIZE 1048576
#define NUM_FILES  256

#define FIRST_DATA_BLOCK 1001 //790 1001

#define READONLY 0x01
#define HIDDEN 0x02

uint8_t data_blocks[NUM_BLOCKS][BLOCK_SIZE];

uint8_t *free_blocks;
uint8_t *free_inodes;

// directory
struct directoryEntry
{
    char filename[64];
    short in_use;
    int32_t inode;
};

struct directoryEntry *directory_ptr;

// inode
struct inode
{
    int32_t blocks[MAX_BLOCKS_PER_FILE];
    short in_use;
    uint32_t file_size;
    time_t date;
    uint8_t attribute;
};

struct inode *inode_ptr;

FILE *disk_image;       // disk image file pointer
char image_name[64];    // disk image filename
uint8_t image_open;

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

#define MAX_NUM_ARGUMENTS 5     // Mav shell only supports four arguments

#define MAX_HISTORY_SIZE 15 // The maximum history size

#define MAX_PID_SIZE 15 // The maximum pids size

int updateHistory(char history[][MAX_COMMAND_SIZE], int history_index, char *command_string);
int updatePids(int pids[MAX_PID_SIZE], int pids_index, int pid);
void trim(char *str);

void init();
int32_t findFreeBlock();
int32_t findFreeInode();
uint32_t df();
uint32_t searchDirectory(char *filename);
void createfs(char *filename);
void savefs();
void openfs(char *filename);
void closefs();
void list(char *attrib1, char *attrib2);
void insert(char *filename);
void attrib(char *attribute, char *filename);
void delete(char *filename);
void undelete(char *filename);

int main() 
{
    init();

    char *command_string = (char*) malloc(MAX_COMMAND_SIZE);

    disk_image = NULL;
    
    char history[MAX_HISTORY_SIZE][MAX_COMMAND_SIZE] = { 0 };
    int history_index = 0;

    int pids[MAX_PID_SIZE] = { 0 };
    int pids_index = 0;
        
    while (1) 
    {
        // Print out the mfs prompt
        printf ("mfs> ");

        // Read the command from the commandline.  The
        // maximum command that will be read is MAX_COMMAND_SIZE
        // This while command will wait here until the user
        // inputs something since fgets returns NULL when there
        // is no input
        while (!fgets(command_string, MAX_COMMAND_SIZE, stdin));
        trim(command_string);

        // If the command line input has '!' as the first character and a history input, 
        // find the corresponding history if there is one
        // and overwrite command_str with the command in history.
        if (command_string[0] == '!' && strlen(command_string) > 1)
        {
            char temp[MAX_COMMAND_SIZE];
            int digit = 1;

            // Copy rest of command_string without '!' into temp.
            strncpy(temp, command_string + 1, strlen(command_string));

            // Check if temp contains any non numeric values
            for (int i = 0; i < strlen(temp); i++)
            {
                if(!isdigit(temp[i]))
                {
                    digit = 0;
                    break;
                }
            }

            // Find the commmand number given and look if it is in history.
            // If not in history, print "Command not in history.", upadate history and pids, and continue.
            // Otherwise, overwrite command_str with the command in history.
            int command_number = atoi(temp);
            if (command_number >= history_index || digit != 1)
            {
                printf("Command not in history.\n");
                history_index = updateHistory(history, history_index, command_string);
                pids_index = updatePids(pids, pids_index, -1);
                continue;
            }
            strcpy(command_string, history[command_number]);
        }

        /* Parse input */
        char *token[MAX_NUM_ARGUMENTS];

        for (int i = 0; i < MAX_NUM_ARGUMENTS; i++)
        {
            token[i] = NULL;
        }

        int token_count = 0;                                 
                                                            
        // Pointer to point to the token
        // parsed by strsep
        char *argument_ptr = NULL;                                         
                                                            
        char *working_string = strdup(command_string);                

        // we are going to move the working_string pointer so
        // keep track of its original value so we can deallocate
        // the correct amount at the end
        char *head_ptr = working_string;

        // Tokenize the input strings with whitespace used as the delimiter
        while ( ( (argument_ptr = strsep(&working_string, WHITESPACE ) ) != NULL) && 
                (token_count < MAX_NUM_ARGUMENTS))
        {
            token[token_count] = strndup(argument_ptr, MAX_COMMAND_SIZE);
            if (strlen(token[token_count]) == 0)
            {
                token[token_count] = NULL;
            }
            token_count++;
        }

        // Continue if user inputs enters a blank line.
        if (token[0] == NULL)
        {
            continue;
        }

        // Program exits if "quit" or "exit" command invoked.
        if ((strcmp("quit", token[0]) == 0) || (strcmp("exit", token[0]) == 0))
        {
            exit(0);
        }

        // If valid directory exists, chdir and update history and pids.
        else if (strcmp("cd", token[0]) == 0)
        {
            if (chdir(token[1]) == -1)
            {
                printf("%s: Directory not found.\n", token[1]);
            }
            
            history_index = updateHistory(history, history_index, command_string);
            pids_index = updatePids(pids, pids_index, -1);
        }

        // If "history" command is invoked without parameters,
        // update history and pids and list the last 15 commands entered by the user.
        else if (strcmp("history", token[0]) == 0 && token[1] == NULL)
        {
            history_index = updateHistory(history, history_index, command_string);
            pids_index = updatePids(pids, pids_index, -1);

            for(int i = 0; i < history_index; i++)
            {
                printf("%2d: %-12s\n", i, history[i]);
            }
        }

        // If "history" command is invoked with parameters,
        // validate parameter and list the last 15 commands, including pids, entered by user.
        else if (strcmp("history", token[0]) == 0 && token[1] != NULL)
        {
            history_index = updateHistory(history, history_index, command_string);
            pids_index = updatePids(pids, pids_index, -1);

            // If parameter is not "-p", print invalid option and continue.
            if (strcmp("-p", token[1]) != 0)
            {
                printf("%s: invalid option -- '%s'\n", token[0], token[1]);
                continue;
            }

            for(int i = 0; i < pids_index; i++)
            {
                printf("%2d: %-12s %d\n", i, history[i], pids[i]);
            }
        }

        // If "createfs" command is invoked.
        else if (strcmp("createfs", token[0]) == 0)
        {
            if (token[1] == NULL)
            {
                printf("createfs: No filename specified.\n");
                continue;
            }
            
            createfs(token[1]);
        }

        // If "savefs" command is invoked.
        else if (strcmp("savefs", token[0]) == 0)
        {
            savefs();
        }

        // If "open" command is invoked.
        else if (strcmp("open", token[0]) == 0)
        {
            if (token[1] == NULL)
            {
                printf("open: No filename specified.\n");
                continue;
            }
            
            openfs(token[1]);
        }

        // If "close" command is invoked.
        else if (strcmp("close", token[0]) == 0)
        {
            closefs();
        }

        // If "list" command is invoked.
        else if (strcmp("list", token[0]) == 0)
        {
            if (image_open == 0)
            {
                printf("list: Disk image is not open.\n");
                continue;
            }

            if (token[1] != NULL && token[2] != NULL)
            {
                if (strcmp(token[1], "-h") == 0 || strcmp(token[1], "-a") == 0)
                {
                    if (strcmp(token[2], "-h") == 0 || strcmp(token[2], "-a") == 0)
                    {
                        list(token[1], token[2]);
                    }
                    else
                    {
                        printf("list: Invalid parameter.\n");
                        continue;
                    }
                }
                else
                {
                    printf("list: Invalid parameter.\n");
                    continue;
                }
            }
            else if (token[1] != NULL)
            {
                if (strcmp(token[1], "-h") == 0 || strcmp(token[1], "-a") == 0)
                {
                    list(token[1], token[2]);
                }
                else
                {
                    printf("list: Invalid parameter.\n");
                    continue;
                }
            }
            else
            {
                list(token[1], token[2]);
            }
        }

        // If "df" command is invoked.
        else if (strcmp("df", token[0]) == 0)
        {
            if (image_open == 0)
            {
                printf("df: Disk image is not open.\n");
                continue;
            }
            
            printf("%d bytes free.\n", df());
        }

        // If "insert" command is invoked.
        else if (strcmp("insert", token[0]) == 0)
        {
            if (image_open == 0)
            {
                printf("insert: Disk image is not open.\n");
                continue;
            }

            if (token[1] == NULL)
            {
                printf("insert: No filename specified.\n");
                continue;
            }
            
            insert(token[1]);
        }

        // If "attrib" command is invoked.
        else if (strcmp("attrib", token[0]) == 0)
        {
            if (image_open == 0)
            {
                printf("attrib: Disk image is not open.\n");
                continue;
            }

            if (token[1] == NULL)
            {
                printf("attrib: No attribute specified.\n");
                continue;
            }

            if (token[2] == NULL)
            {
                printf("attrib: No filename specified.\n");
                continue;
            }

            attrib(token[1], token[2]);
        }

        // If "delete" command is invoked.
        else if (strcmp("delete", token[0]) == 0)
        {
            if (image_open == 0)
            {
                printf("delete: Disk image is not open.\n");
                continue;
            }

            if (token[1] == NULL)
            {
                printf("delete: No filename specified.\n");
                continue;
            }

            delete(token[1]);
        }

        // If "undelete" command is invoked.
        else if (strcmp("undelete", token[0]) == 0)
        {
            if (image_open == 0)
            {
                printf("undelete: Disk image is not open.\n");
                continue;
            }

            if (token[1] == NULL)
            {
                printf("undelete: No filename specified.\n");
                continue;
            }

            undelete(token[1]);
        }

        // Fork calls for UNIX commands.
        else
        {
            pid_t pid = fork();

            // Process failed
            if (pid == -1) 
            {
                perror("fork failed: ");
                exit(1);
            }

            // Child process
            if (pid == 0) 
            {
                // Call process in command_line with parameters.
                int ret = execvp(token[0], token);
                if (ret == -1)
                {
                printf("%s: Command not found.\n", token[0]);
                }

                // Exit child process before the parent process.
                exit(1);
            }

            // Parent process
            else 
            {
                // Wait for child process to terminate.
                int status;
                waitpid(pid, &status, 0);

                // Update history and pids.
                history_index = updateHistory(history, history_index, command_string);
                pids_index = updatePids(pids, pids_index, pid);

                fflush(NULL);
            }
        }

        // Cleanup allocated memory
        for (int i = 0; i < MAX_NUM_ARGUMENTS; i++)
        {
            if (token[i] != NULL)
            {
                free(token[i]);
            }
        }

        free(head_ptr);
    }

    free(command_string);

    return 0;
}

// Updates history array with 15 most recent commands.
int updateHistory(char history[][MAX_COMMAND_SIZE], int history_index, char *command_string)
{
    // If history is full, pop first command and push new command.
    // Reorder history array.
    if (history_index == MAX_HISTORY_SIZE)
    {  
        for (int i = 0; i < MAX_HISTORY_SIZE - 1; i++)
        {
            strcpy(history[i], history[i+1]);
        }
        strcpy(history[MAX_HISTORY_SIZE-1], command_string);
    }

    // If history is not full, add new command to history and increase history_index.
    else
    {
        strcpy(history[history_index], command_string);
        history_index++;
    }

    return history_index;
}

// Updates pids array with 15 most recent PID commands.
int updatePids(int pids[MAX_PID_SIZE], int pids_index, int pid)
{
    // If pids is full, pop first PID and push new PID.
    // Reorder pid array.
    if (pids_index == MAX_PID_SIZE)
    {
        for (int i = 0; i < MAX_PID_SIZE - 1; i++)
        {
            pids[i] = pids[i+1];
        }
        pids[MAX_PID_SIZE-1] = pid;
    }

    // If pids is not full, add new PID to history and increase pids_index.
    else
    {
        pids[pids_index] = pid;
        pids_index++;
    }

    return pids_index++;
}

// Trim newline character.
void trim(char *str)
{
    int l = strlen(str);

    if (str[l-1] == '\n')
    {
        str[l-1] = 0;
    }
}

void init()
{
    directory_ptr = (struct directoryEntry *)&data_blocks[0][0];
    inode_ptr = (struct inode *)&data_blocks[20][0];
    free_blocks = (uint8_t *)&data_blocks[1000][0]; //277 1000
    free_inodes = (uint8_t *)&data_blocks[19][0];
 
    memset(image_name, 0, 64);
    image_open = 0;

	for(int i = 0; i < NUM_FILES; i++)
	{
        memset(directory_ptr[i].filename, 0, 64);
		directory_ptr[i].in_use = 0;
        directory_ptr[i].inode = -1;
        free_inodes[i] = 1;

        for (int j = 0; j < NUM_BLOCKS; j++)
        {
            inode_ptr[i].blocks[j] = -1;
            inode_ptr[i].in_use = 0;
            inode_ptr[i].file_size = 0;
            inode_ptr[i].date = -1;
            inode_ptr[i].attribute = 0;
            inode_ptr[i].attribute &= ~HIDDEN;
            inode_ptr[i].attribute &= ~READONLY;
        }
	}

    for(int i = 0; i < NUM_BLOCKS; i++)
    {
        free_blocks[i] = 1;
    }
}

int32_t findFreeBlock()
{
    for (int i = 0; i < NUM_BLOCKS; i++)
    {
        if (free_blocks[i])
        {
            // free_blocks[i + 790] = 0;
            return i + 1001; //790 1001 1065
        }
    }
    return -1;
}

int32_t findFreeInode()
{
    for (int i = 0; i < NUM_FILES; i++)
    {
        if (free_inodes[i] == 1)
        {
            free_inodes[i] = 0;
            return i;
        }
    }
    return -1;
}

int32_t findFreeInodeBlock(int32_t inode)
{
    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++)
    {
        if (inode_ptr[inode].blocks[i] == -1)
        {
            return i;
        }
    }
    return -1;
}

uint32_t df()
{
    int count = 0;
    
    for (int i = FIRST_DATA_BLOCK; i < NUM_BLOCKS; i++)
    {
        if (free_blocks[i])
        {
            count++;
        }
    }

    return count * BLOCK_SIZE;
}

// Search the directory for filename
uint32_t searchDirectory(char *filename)
{
    int ret = -1;

    for (int i = 0; i < NUM_FILES; i++) 
	{
		if(directory_ptr[i].filename == NULL)
		{
			continue;
		}	
        else if(strcmp(filename, directory_ptr[i].filename) == 0) 
		{
            return i;
        }
    }

    return ret;	
}	

void createfs(char *filename)
{
    if(strlen(filename) > 64)
	{
		printf("createfs: Only supports filenames of up to 64 characters.\n");
		return;
	}
	
	disk_image = fopen(filename, "w");
    strncpy(image_name, filename, strlen(filename));

    memset(data_blocks, 0, NUM_BLOCKS * BLOCK_SIZE);
    image_open = 1;

    for(int i = 0; i < NUM_FILES; i++)
	{
        memset(directory_ptr[i].filename, 0, 64);
		directory_ptr[i].in_use = 0;
        directory_ptr[i].inode = -1;
        free_inodes[i] = 1;

        for (int j = 0; j < NUM_BLOCKS; j++)
        {
            inode_ptr[i].blocks[j] = -1;
            inode_ptr[i].in_use = 0;
            inode_ptr[i].file_size = 0;
            inode_ptr[i].date = -1;
            inode_ptr[i].attribute = 0;
            inode_ptr[i].attribute &= ~HIDDEN;
            inode_ptr[i].attribute &= ~READONLY;
        }
	}

    for(int i = 0; i < NUM_BLOCKS; i++)
    {
        free_blocks[i] = 1;
    }

    fclose(disk_image);
}

void savefs()
{
    if (image_open == 0)
    {
        printf("savefs: Disk image is not open.\n");
        return;
    }

    disk_image = fopen(image_name, "w");

	if(disk_image == NULL)
	{
		printf("savefs: Disk image filename not found.\n");
	}

	fwrite(&data_blocks[0][0], BLOCK_SIZE, NUM_BLOCKS, disk_image);

    memset(image_name, 0, 64);

	fclose(disk_image);	
}

void openfs(char *filename)
{
    disk_image = fopen(filename, "r");

    if(disk_image == NULL)
	{
		printf("open: Disk image filename not found.\n");
		return;
	}

    strncpy(image_name, filename, strlen(filename));

    fread(&data_blocks[0][0], BLOCK_SIZE, NUM_BLOCKS, disk_image);
    image_open = 1;
}

void closefs()
{
    if (image_open == 0)
    {
        printf("close: Disk image is not open.\n");
        return;
    }

    if(disk_image == NULL)
	{
		printf("close: Disk image filename not found.\n");
		return;
	}

    fclose(disk_image);
    image_open = 0;

    memset(image_name, 0, 64);
}

void list(char *attrib1, char *attrib2)
{
    int not_found = 1;

    for (int i = 0; i < NUM_FILES; i++)
    {
        int32_t inode_index = directory_ptr[i].inode;

        if (directory_ptr[i].in_use)
        {
            if (attrib1 == NULL && attrib2 == NULL)
            {
                if (!(inode_ptr[inode_index].attribute & HIDDEN))
                {
                    not_found = 0;
                    char filename[65];
                    memset(filename, 0, 65);
                    strncpy(filename, directory_ptr[i].filename, strlen(directory_ptr[i].filename));

                    char *date = ctime(&inode_ptr[inode_index].date);
                    trim(date);
                    
                    printf("%d %s %s\n", inode_ptr[inode_index].file_size, date, filename);
                }
            }
            else if (attrib1 != NULL && strcmp(attrib1, "-h") == 0)
            {
                if (attrib2 != NULL && strcmp(attrib2, "-a") == 0)
                {
                    not_found = 0;
                    char filename[65];
                    memset(filename, 0, 65);
                    strncpy(filename, directory_ptr[i].filename, strlen(directory_ptr[i].filename));

                    char *date = ctime(&inode_ptr[inode_index].date);
                    trim(date);
                    
                    printf("%d %s %s ", 
                    inode_ptr[inode_index].file_size, date, filename);

                    // Print the value of the attribute as an 8 bit binary value
                    printf("%d%d%d%d%d%d%d%d\n",
                    (inode_ptr[inode_index].attribute >> 7) & 0x01,
                    (inode_ptr[inode_index].attribute >> 6) & 0x01,
                    (inode_ptr[inode_index].attribute >> 5) & 0x01,
                    (inode_ptr[inode_index].attribute >> 4) & 0x01,
                    (inode_ptr[inode_index].attribute >> 3) & 0x01,
                    (inode_ptr[inode_index].attribute >> 2) & 0x01,
                    (inode_ptr[inode_index].attribute >> 1) & 0x01,
                     inode_ptr[inode_index].attribute & 0x01);
                }
                else
                {
                    not_found = 0;
                    char filename[65];
                    memset(filename, 0, 65);
                    strncpy(filename, directory_ptr[i].filename, strlen(directory_ptr[i].filename));

                    char *date = ctime(&inode_ptr[inode_index].date);
                    trim(date);
                    
                    printf("%d %s %s\n", inode_ptr[inode_index].file_size, date, filename);
                }
            }
            else if (attrib1 != NULL && strcmp(attrib1, "-a") == 0)
            {
                if (attrib2 != NULL && strcmp(attrib2, "-h") == 0)
                {
                    not_found = 0;
                    char filename[65];
                    memset(filename, 0, 65);
                    strncpy(filename, directory_ptr[i].filename, strlen(directory_ptr[i].filename));

                    char *date = ctime(&inode_ptr[inode_index].date);
                    trim(date);
                    
                    printf("%d %s %s ", 
                    inode_ptr[inode_index].file_size, date, filename);

                    // Print the value of the attribute as an 8 bit binary value
                    printf("%d%d%d%d%d%d%d%d\n",
                    (inode_ptr[inode_index].attribute >> 7) & 0x01,
                    (inode_ptr[inode_index].attribute >> 6) & 0x01,
                    (inode_ptr[inode_index].attribute >> 5) & 0x01,
                    (inode_ptr[inode_index].attribute >> 4) & 0x01,
                    (inode_ptr[inode_index].attribute >> 3) & 0x01,
                    (inode_ptr[inode_index].attribute >> 2) & 0x01,
                    (inode_ptr[inode_index].attribute >> 1) & 0x01,
                     inode_ptr[inode_index].attribute & 0x01);
                }
                else
                {
                    if (!(inode_ptr[inode_index].attribute & HIDDEN))
                    {
                        not_found = 0;
                        char filename[65];
                        memset(filename, 0, 65);
                        strncpy(filename, directory_ptr[i].filename, strlen(directory_ptr[i].filename));

                        char *date = ctime(&inode_ptr[inode_index].date);
                        trim(date);
                        
                        printf("%d %s %s ", 
                        inode_ptr[inode_index].file_size, date, filename);

                        // Print the value of the attribute as an 8 bit binary value
                        printf("%d%d%d%d%d%d%d%d\n",
                        (inode_ptr[inode_index].attribute >> 7) & 0x01,
                        (inode_ptr[inode_index].attribute >> 6) & 0x01,
                        (inode_ptr[inode_index].attribute >> 5) & 0x01,
                        (inode_ptr[inode_index].attribute >> 4) & 0x01,
                        (inode_ptr[inode_index].attribute >> 3) & 0x01,
                        (inode_ptr[inode_index].attribute >> 2) & 0x01,
                        (inode_ptr[inode_index].attribute >> 1) & 0x01,
                        inode_ptr[inode_index].attribute & 0x01);
                    }
                }
            }
        }
    }
    
    if (not_found)
    {
        printf("list: No files found.\n");
    }
}

void insert(char *filename)
{
    // Verify the filename isn't NULL.
    if (filename == NULL)
    {
        printf("insert: Filename is NULL\n");
        return;
    }

    // Verify the file exists.
    struct stat buf;
    int ret = stat(filename, &buf);

    if (ret == -1)
    {
        printf("insert: File does not exist.\n");
        return;
    }

    // Verify the file isn't too big.
    if (buf.st_size > MAX_FILE_SIZE)
    {
        printf("insert: File is too large.\n");
        return;
    }

    // Verify that there is enough disk space.
    if (buf.st_size > df())
    {
        printf("insert: Not enough free disk space.\n");
        return;
    }

    // Find an empty directory entry
    int directory_entry = -1;

    for (int i = 0; i < NUM_FILES; i++)
    {
        if (directory_ptr[i].in_use == 0)
        {
            directory_entry = i;
            break;
        }
    }

    if (directory_entry == -1)
    {
        printf("insert: Could not find a free directory entry.\n");
        return;
    }

    // Open the input file read-only 
    FILE *ifp = fopen (filename, "r"); 
    printf("Reading %d bytes from %s\n", (int)buf.st_size, filename);
 
    // Save off the size of the input file since we'll use it in a couple of places and 
    // also initialize our index variables to zero. 
    int32_t copy_size = buf.st_size;

    // We want to copy and write in chunks of BLOCK_SIZE. So to do this 
    // we are going to use fseek to move along our file stream in chunks of BLOCK_SIZE.
    // We will copy bytes, increment our file pointer by BLOCK_SIZE and repeat.
    int32_t offset = 0;               

    // We are going to copy and store our file in BLOCK_SIZE chunks instead of one big 
    // memory pool. Why? We are simulating the way the file system stores file data in
    // blocks of space on the disk. block_index will keep us pointing to the area of
    // the area that we will read from or write to.
    int32_t block_index = -1;

    // Find a free inode.
    int32_t inode_index = findFreeInode();
    if (inode_index == -1)
    {
        printf("insert: Can not find a free inode.\n");
        return;
    }

    // Place the file info in the directory
    directory_ptr[directory_entry].in_use = 1;
    directory_ptr[directory_entry].inode = inode_index;
    strncpy(directory_ptr[directory_entry].filename, filename, strlen(filename));

    // Place the file info in the inode
    inode_ptr[inode_index].file_size = buf.st_size;
    inode_ptr[inode_index].in_use = 1;
    inode_ptr[inode_index].date = time(NULL);
    inode_ptr[inode_index].attribute &= ~HIDDEN;
    inode_ptr[inode_index].attribute &= ~READONLY;
 
    // copy_size is initialized to the size of the input file so each loop iteration we
    // will copy BLOCK_SIZE bytes from the file then reduce our copy_size counter by
    // BLOCK_SIZE number of bytes. When copy_size is less than or equal to zero we know
    // we have copied all the data from the input file.
    while (copy_size > 0)
    {
        // Index into the input file by offset number of bytes.  Initially offset is set to
        // zero so we copy BLOCK_SIZE number of bytes from the front of the file.  We 
        // then increase the offset by BLOCK_SIZE and continue the process.  This will
        // make us copy from offsets 0, BLOCK_SIZE, 2*BLOCK_SIZE, 3*BLOCK_SIZE, etc.
        fseek(ifp, offset, SEEK_SET);
    
        // Read BLOCK_SIZE number of bytes from the input file and store them in our
        // data array. 

        // Find a free block.
        block_index = findFreeBlock();

        if (block_index == -1)
        {
            printf("insert: Can not find a free block.\n");
            return;
        }

        int32_t bytes = fread(data_blocks[block_index], BLOCK_SIZE, 1, ifp);

        // Save the block in the inode
        int32_t inode_block = findFreeInodeBlock(inode_index);
        inode_ptr[inode_index].blocks[inode_block] = block_index;

        // If bytes == 0 and we haven't reached the end of the file then something is 
        // wrong. If 0 is returned and we also have the EOF flag set then that is OK.
        // It means we've reached the end of our input file.
        if (bytes == 0 && !feof(ifp))
        {
            printf("An error occured reading from the input file.\n");
            return;
        }

        // Clear the EOF file flag.
        clearerr(ifp);

        // Reduce copy_size by the BLOCK_SIZE bytes.
        copy_size -= BLOCK_SIZE;
        
        // Increase the offset into our input file by BLOCK_SIZE.  This will allow
        // the fseek at the top of the loop to position us to the correct spot.
        offset += BLOCK_SIZE;

        block_index = findFreeBlock();
    }

    // We are done copying from the input file so close it out.
    fclose(ifp);
}

// Edits attributes for files in the system
void attrib(char *attribute, char *filename)
{
    // Verify the filename isn't NULL.
    if (filename == NULL)
    {
        printf("attrib: Filename is NULL\n");
        return;
    }

	int directory_entry = searchDirectory(filename);
	
    if (directory_entry == -1)
	{	
		printf("attrib: File not found in directory.\n");
        return;
	}
	
	int inode_index = directory_ptr[directory_entry].inode;
	
	if (strcmp(attribute, "+h") == 0)
	{
		inode_ptr[inode_index].attribute |= HIDDEN;
	}	
	else if (strcmp(attribute, "+r") == 0)
	{
		inode_ptr[inode_index].attribute |= READONLY;
	}
	else if (strcmp(attribute, "-h") == 0)
	{
		inode_ptr[inode_index].attribute &= ~HIDDEN;
	}
	else if (strcmp(attribute, "-r") == 0)
	{
		inode_ptr[inode_index].attribute &= ~READONLY;
	}
}

void delete(char *filename)
{
    // Verify the filename isn't NULL.
    if (filename == NULL)
    {
        printf("delete: Filename is NULL\n");
        return;
    }

	int directory_entry = searchDirectory(filename);

	if (directory_entry == -1)
	{	
		printf("delete: File not found in directory.\n");
        return;
	}

	int inode_index = directory_ptr[directory_entry].inode;
	
	if (inode_ptr[inode_index].attribute & READONLY)
	{
		printf("delete: The file is marked read-only and can not be deleted.\n");
		return;
	}

    directory_ptr[directory_entry].in_use = 0;
	inode_ptr[inode_index].in_use = 0;
    free_inodes[inode_index] = 1;

    for(int i = 0; i < MAX_BLOCKS_PER_FILE; i++) 
    {
        int block_index = inode_ptr[inode_index].blocks[i];
        free_blocks[block_index] = 1;
    }
}

void undelete(char *filename)
{
    // Verify the filename isn't NULL.
    if (filename == NULL)
    {
        printf("undelete: Filename is NULL\n");
        return;
    }

	int directory_entry = searchDirectory(filename);

	if (directory_entry == -1)
	{	
		printf("undelete: File not found in directory.\n");	
	}

    int inode_index = directory_ptr[directory_entry].inode;

    directory_ptr[directory_entry].in_use = 1;
	inode_ptr[inode_index].in_use = 1;
    free_inodes[inode_index] = 0;

    for(int i = 0; i < MAX_BLOCKS_PER_FILE; i++) 
    {
        int block_index = inode_ptr[inode_index].blocks[i];
        free_blocks[block_index] = 0;
    }
}

