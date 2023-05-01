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
void readFile(char *filename, int start, int num_bytes);
void retrieve(char *filename, char *new_filename);
void readFileRetrieve(char *filename, int directory_entry);
void encrypt(char *filename, char cipher);
void decrypt(char *filename, char cipher);
uint8_t hex_to_byte(char *hex);

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
                if (!isdigit(temp[i]))
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

            for (int i = 0; i < history_index; i++)
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

            for (int i = 0; i < pids_index; i++)
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

        // If "read" command is invoked.
        else if (strcmp("read", token[0]) == 0)
        {
            if (image_open == 0)
            {
                printf("read: Disk image is not open.\n");
                continue;
            }

            if (token[1] == NULL)
            {
                printf("read: No filename specified.\n");
                continue;
            }
            else if (token[2] == NULL)
            {
                printf("read: No starting byte specified.\n");
                continue;
            }
            else if (token[3] == NULL)
            {
                printf("read: No number of bytes specified.\n");
                continue;
            }

            readFile(token[1], atoi(token[2]), atoi(token[3]));
        }

        // If "retrieve" command is invoked.
        else if (strcmp("retrieve", token[0]) == 0)
        {
            if (image_open == 0)
            {
                printf("retrieve: Disk image is not open.\n");
                continue;
            }

            if (token[1] == NULL)
            {
                printf("retrieve: No filename specified.\n");
                continue;
            }

            retrieve(token[1], token[2]);
        }

        // If "encrypt" command is invoked.
        else if (strcmp("encrypt", token[0]) == 0)
        {
            if (image_open == 0)
            {
                printf("encrypt: Disk image is not open.\n");
                continue;
            }

            if (token[1] == NULL)
            {
                printf("encrypt: No filename specified.\n");
                continue;
            }

            if (token[2] == NULL)
            {
                printf("encrypt: No cipher specified.\n");
                continue;
            }

            uint8_t hex = hex_to_byte(token[2]);

            if (hex == -1)
            {
                printf("encrypt: Invalid cipher.\n");
                continue;
            }

            encrypt(token[1], hex);
        }

        // If "decrypt" command is invoked.
        else if (strcmp("decrypt", token[0]) == 0)
        {
            if (image_open == 0)
            {
                printf("decrypt: Disk image is not open.\n");
                continue;
            }

            if (token[1] == NULL)
            {
                printf("decrypt: No filename specified.\n");
                continue;
            }

            if (token[2] == NULL)
            {
                printf("decrypt: No cipher specified.\n");
                continue;
            }
            
            uint8_t hex = hex_to_byte(token[2]);

            if (hex == -1)
            {
                printf("decrypt: Invalid cipher.\n");
                continue;
            }

            decrypt(token[1], hex);
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

// Initializes values.
void init()
{
    // Input: None
    // Output: Void. Initializes values related to file system.
    // Description: Zeros out data of file system related values.
    directory_ptr = (struct directoryEntry *)&data_blocks[0][0];
    inode_ptr = (struct inode *)&data_blocks[20][0];
    free_blocks = (uint8_t *)&data_blocks[1000][0]; //277 1000
    free_inodes = (uint8_t *)&data_blocks[19][0];
 
    memset(image_name, 0, 64);
    image_open = 0;

	for (int i = 0; i < NUM_FILES; i++)
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

    for (int i = 0; i < NUM_BLOCKS; i++)
    {
        free_blocks[i] = 1;
    }
}

// Finds free block in free_blocks[] array.
int32_t findFreeBlock()
{
    // Input: None
    // Output: int32_t. Returns free block.
    // Description: Loops through free_blocks[] up till NUM_BLOCKS. If a free block is found,
    //              index of block plus 1001 is returnd and that block is marked not free.
    //              Returns -1 if no free blocks are found.

    for (int i = 0; i < NUM_BLOCKS; i++)
    {
        if (free_blocks[i])
        {
            // free_blocks[i + 790] = 0;
            free_blocks[i + 1001] = 0;
            return i + 1001; //790 1001 1065
        }
    }
    return -1;
}

// Finds free inode in free_inodes[] array
int32_t findFreeInode()
{
    // Input: None
    // Output: int32_t. Returns free inode.
    // Description: Loops through free_inodes[] up till NUM_FILESS. If a free inode is found,
    //              index of free inode is returnd and that inode is marked not free.
    //              Returns -1 if no free inodes are found.

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
// Finds free inode block in inode_ptr[inode].blocks[] array
int32_t findFreeInodeBlock(int32_t inode)
{
    // Input: int32_t inode - directory[directory_entry].inode
    // Output: int32_t. Returns free inode block.
    // Description: Loops through inode_ptr[inode].blocks[] up till MAX_BLOCKS_PER_FILE.
    //              If a free inode block is found,
    //              index of inode block is returned.
    //              Returns -1 if no free inode blocks are found.

    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++)
    {
        if (inode_ptr[inode].blocks[i] == -1)
        {
            return i;
        }
    }
    return -1;
}

// The df command.
uint32_t df()
{
    // Input: None.
    // Output: uint32_t. Returns the amount of free space in the file system in bytes.
    // Description: Loops through free_blocks[] starting at FIRST_DATA_BLOCK 
    //              and ends at NUM_BLOCKS. int count is increased for every
    //              free block found.
    //              Returns count * BLOCK_SIZE.

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

// Searches the directory for filename
uint32_t searchDirectory(char *filename)
{
    // Input: char *filename - the filename to look for.
    // Output: uint32_t. Returns the index of the filename in directory_ptr[] if found.
    //         Returns -1 if filename is not found in directory.
    // Description: Loops through directory_ptr[] starting at 0 
    //              and ends at NUM_FILES. If filename == directory_ptr[i].filename,
    //              index of filename is returned.
    //              Returns -1 if filename is not found in directory.

    int ret = -1;

    for (int i = 0; i < NUM_FILES; i++) 
	{
		if (directory_ptr[i].filename == NULL)
		{
			continue;
		}	
        else if (strcmp(filename, directory_ptr[i].filename) == 0) 
		{
            return i;
        }
    }

    return ret;	
}	

// The createfs command.
void createfs(char *filename)
{
    // Input: char *filename - the name of the file system.
    // Output: void. Creates the file system.
    // Description: Initializes the file system in the disk_image.

    if (strlen(filename) > 64)
	{
		printf("createfs: Only supports filenames of up to 64 characters.\n");
		return;
	}
	
	disk_image = fopen(filename, "w");
    strncpy(image_name, filename, strlen(filename));

    memset(data_blocks, 0, NUM_BLOCKS * BLOCK_SIZE);
    image_open = 1;

    for (int i = 0; i < NUM_FILES; i++)
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

    for (int i = 0; i < NUM_BLOCKS; i++)
    {
        free_blocks[i] = 1;
    }

    fclose(disk_image);
}

// The savefs command.
void savefs()
{
    // Input: None.
    // Output: void. Saves the file system.
    // Description: If image is open and not NULL, the data_blocks are
    //              written to the disK_image.

    if (image_open == 0)
    {
        printf("savefs: Disk image is not open.\n");
        return;
    }

    disk_image = fopen(image_name, "w");

	if (disk_image == NULL)
	{
		printf("savefs: Disk image filename not found.\n");
        return;
	}

	fwrite(&data_blocks[0][0], BLOCK_SIZE, NUM_BLOCKS, disk_image);

    memset(image_name, 0, 64);

	fclose(disk_image);	
}

// The openfs command.
void openfs(char *filename)
{
    // Input: char *filename - name of the file system to open.
    // Output: void. Opens the file system.
    // Description: If image is not open and not NULL, image_name is copied from
    //              filename and the disk_image reads the data from data_blocks.
    //              Image is set to open.

    disk_image = fopen(filename, "r");

    if (disk_image == NULL)
	{
		printf("open: Disk image filename not found.\n");
		return;
	}

    if (image_open == 1)
    {
        printf("open: Disk image is already open.\n");
		return;
    }

    strncpy(image_name, filename, strlen(filename));

    fread(&data_blocks[0][0], BLOCK_SIZE, NUM_BLOCKS, disk_image);
    image_open = 1;
}

// The closefs command.
void closefs()
{
    // Input: None.
    // Output: void. Closes the file system.
    // Description: If image is open and not NULL, disk_image is closed,
    //              image is set to closed, and image_name is zeroed out.

    if (image_open == 0)
    {
        printf("close: Disk image is not open.\n");
        return;
    }

    if (disk_image == NULL)
	{
		printf("close: Disk image filename not found.\n");
		return;
	}

    fclose(disk_image);
    image_open = 0;

    memset(image_name, 0, 64);
}

// The list command.
void list(char *attrib1, char *attrib2)
{
    // Input: char *attrib1 - First attribute.
    //        char *attrib2 - Second attribute.
    // Output: void. Lists the file system.
    // Description: Loops through the files within the file system.
    //              If an attribute is '-h', hidden files are displayed.
    //              If an attribute is '-a', attributes are displayed
    //              as an 8 bit value.

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

// The insert command.
void insert(char *filename)
{
    // Input: char *filename - The file to put into the file system.
    // Output: void. Inserts filename into the file system.
    // Description: After checking if the file exists, the input read-only
    //              file is open. Data is copied and stored into 
    //              the file system in BLOCK_SIZE chunks

    // Verify the filename isn't NULL.
    if (filename == NULL)
    {
        printf("insert: Filename is NULL\n");
        return;
    }

    // Verify name isn't too long.
    if (strlen(filename) > 64)
	{
		printf("insert: Only supports filenames of up to 64 characters.\n");
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
    int directory_entry = searchDirectory(filename);
    int rewrite = 1;

    if (directory_entry == -1)
    {
        rewrite = 0;
        for (int i = 0; i < NUM_FILES; i++)
        {
            if (directory_ptr[i].in_use == 0)
            {
                directory_entry = i;
                break;
            }
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
    int32_t inode_index = -1;

    if (rewrite == 1)
    {
        inode_index = directory_ptr[directory_entry].inode;
    }
    else
    {
        inode_index = findFreeInode();
    }

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

    int i = 0;
 
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

        // if (rewrite == 1)
        // {
        //     block_index = inode_ptr[inode_index].blocks[i++];
        // }
        // else
        // {
        //     block_index = findFreeBlock();
        // }

        block_index = findFreeBlock();

        if (block_index == -1)
        {
            printf("insert: Can not find a free block.\n");
            return;
        }

        int32_t bytes = fread(data_blocks[block_index], BLOCK_SIZE, 1, ifp);

        // Save the block in the inode
        int32_t inode_block = -1;

        if (rewrite == 1)
        {
            inode_block = inode_ptr[inode_index].blocks[i++];
        }
        else
        {
            inode_block = findFreeInodeBlock(inode_index);
        }
        
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

        // block_index = findFreeBlock();
    }

    // We are done copying from the input file so close it out.
    fclose(ifp);
}

// The attrib command.
void attrib(char *attribute, char *filename)
{
    // Input: char *attribute - the attribute we want to add/subtract.
    //        char *filename - The file we want to edit.
    // Output: void. Sets or removes an attribute from the file.
    // Description: After checking if the file exists in the directory, 
    //              if-else statements set/remove the attribute bit
    //              in the file.

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
    if (directory_ptr[directory_entry].in_use == 0)
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

// The delete command.
void delete(char *filename)
{
    // Input: char *filename - The file we want to delete.
    // Output: void. deletes a file from the file system.
    // Description: After checking if the file exists in the directory, 
    //              directory_entry, inode_index, and blocks
    //              associated with the inode are set to free.

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

    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) 
    {
        int block_index = inode_ptr[inode_index].blocks[i];
        free_blocks[block_index] = 1;
    }
}

// The undelete command.
void undelete(char *filename)
{
    // Input: char *filename - The file we want to undelete.
    // Output: void. undeletes a file from the file system.
    // Description: After checking if the file exists in the directory, 
    //              directory_entry, inode_index, and blocks
    //              associated with the inode are set to in use.

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
        return;
	}

    int inode_index = directory_ptr[directory_entry].inode;

    directory_ptr[directory_entry].in_use = 1;
	inode_ptr[inode_index].in_use = 1;
    free_inodes[inode_index] = 0;

    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) 
    {
        int block_index = inode_ptr[inode_index].blocks[i];
        free_blocks[block_index] = 0;
    }
}

// The read command.
void readFile(char *filename, int start, int num_bytes)
{
    // Input: char *filename - The file we want to read.
    //        int start - the bytes we want to start reading the file at.
    //        int num_bytes - the number of bytes we want to read.
    // Output: void. reads a file from the file system and prints its
    //         hexadecimal values.
    // Description: After checking if the file exists in the directory, 
    //              fseek is used to go to the start pos.
    //              fread reads the number of bytes and is stored in buffer[num_bytes];
    //              for loop prints what's stored in buffer as hexadecimal values.
    
    // Verify file is in directory
    int directory_entry = searchDirectory(filename);
	if (directory_entry == -1)
	{	
		printf("read: File not found in directory.\n");
        return;
	}
    if (directory_ptr[directory_entry].in_use == 0)
    {
        printf("read: File not found in directory.\n");
        return;
    }

    readFileRetrieve(filename, directory_entry);

    FILE *fp = fopen("temp", "rb");
    
    if (fp == NULL) 
    { 
        printf("read: Can not open file.\n");
        return;
    }

    uint8_t buffer[num_bytes];

    fseek(fp, start + 1, SEEK_CUR);
    fread(buffer, num_bytes, 1, fp);

    for (int i = 0; i < num_bytes; i++)
    {
        printf("%02x ", buffer[i]);
    }

    fclose(fp);
    remove("temp");

    printf("\n");
}

// The retrieve command.
void retrieve(char *filename, char *new_filename)
{
    // Input: char *filename - The file we want to retrieve.
    //        char *new_filename - Optional name for the retrieved file.
    // Output: void. retrieve a file from the file system and place it 
    //               in the current working directory.
    // Description: After checking if the file exists in the directory, 
    //              output file is opened. Using copy_size as a count to determine 
    //              when we've copied enough bytes to the output file.

    // Verify the filename isn't NULL.
    if (filename == NULL)
    {
        printf("retrieve: Filename is NULL\n");
        return;
    }

    // Verify file is in directory
    int directory_entry = searchDirectory(filename);
	if (directory_entry == -1)
	{	
		printf("retrieve: File not found in directory.\n");
        return;
	}
    if (directory_ptr[directory_entry].in_use == 0)
    {
        printf("retrieve: File not found in directory.\n");
        return;
    }
    
    FILE *ofp;

    if (new_filename != NULL)
    {
        ofp = fopen(new_filename, "w");
    }
    else
    {
        ofp = fopen(filename, "w");
    }

    if (ofp == NULL)
    {
        printf("retrieve: Could not open output file: %s\n", filename);
        return;
    }

    int block_pos = 0;
    int inode_index = directory_ptr[directory_entry].inode;
	int block_index = inode_ptr[inode_index].blocks[block_pos];

    int copy_size = inode_ptr[inode_index].file_size;
    int offset = 0;

    printf("Writing %d bytes to %s\n", inode_ptr[inode_index].file_size, filename);

    // Using copy_size as a count to determine when we've copied enough bytes to the output file.
    // Each time through the loop, except the last time, we will copy BLOCK_SIZE number of bytes from
    // our stored data to the file fp, then we will increment the offset into the file we are writing to.
    // On the last iteration of the loop, instead of copying BLOCK_SIZE number of bytes we just copy
    // how ever much is remaining ( copy_size % BLOCK_SIZE ).  If we just copied BLOCK_SIZE on the
    // last iteration we'd end up with gibberish at the end of our file. 
    while (copy_size > 0)
    { 
        int num_bytes;

        // If the remaining number of bytes we need to copy is less than BLOCK_SIZE then
        // only copy the amount that remains. If we copied BLOCK_SIZE number of bytes we'd
        // end up with garbage at the end of the file.
        if (copy_size < BLOCK_SIZE)
        {
            num_bytes = copy_size;
        }
        else 
        {
            num_bytes = BLOCK_SIZE;
        }

        // Write num_bytes number of bytes from our data array into our output file.
        fwrite(data_blocks[block_index], num_bytes, 1, ofp); 

        // Reduce the amount of bytes remaining to copy, increase the offset into the file
        // and increment the block_pos to move us to the next data block.
        copy_size -= BLOCK_SIZE;
        offset += BLOCK_SIZE;
        block_pos++;
        block_index = inode_ptr[inode_index].blocks[block_pos];	     

        // Since we've copied from the point pointed to by our current file pointer, increment
        // offset number of bytes so we will be ready to copy to the next area of our output file.
        fseek(ofp, offset, SEEK_SET);
    }

    // Close the output file, we're done. 
    fclose(ofp);
}

// Useful for reading a file in the file system.
void readFileRetrieve(char *filename, int directory_entry)
{
    // Input: char *filename - The file we want to retrieve.
    //        int directory_entry - The index for the file in the directory.
    // Output: void. Creates a file for read() to read from.
    // Description: The data from the file in the the file system is put
    //              into the cwd. The file in the cwd is used to read the
    //              bytes of the file we want to read.

    // Verify the filename isn't NULL.
    if (filename == NULL)
    {
        printf("read: Filename is NULL\n");
        return;
    }

    FILE *ofp;

    ofp = fopen("temp", "w");

    if (ofp == NULL)
    {
        printf("read: Could not open file: %s\n", filename);
        return;
    }

    int block_pos = 0;
    int inode_index = directory_ptr[directory_entry].inode;
	int block_index = inode_ptr[inode_index].blocks[block_pos];

    int copy_size = inode_ptr[inode_index].file_size;
    int offset = 0;

    while (copy_size > 0)
    {
        int num_bytes;

        if (copy_size < BLOCK_SIZE)
        {
            num_bytes = copy_size;
        }
        else 
        {
            num_bytes = BLOCK_SIZE;
        }

        fwrite(data_blocks[block_index], num_bytes, 1, ofp); 

        copy_size -= BLOCK_SIZE;
        offset += BLOCK_SIZE;
        block_pos++;
        block_index = inode_ptr[inode_index].blocks[block_pos];	     

        fseek(ofp, offset, SEEK_SET);
    }

    // Close the output file, we're done. 
    fclose(ofp);
}

// The encrypt command.
void encrypt(char *filename, char cipher)
{
    // Input: char *filename - The file we want to encrypt.
    //        char *cipher - The cipher used to encrypt the file.
    // Output: void. Encrypts the file using the given cipher.
    // Description: The data from the file in the the file system is put
    //              into the cwd. The file in the cwd is used to read the
    //              bytes of the file we want to encrypt. Each byte in
    //              the file is encrypted using the XOR cipher. 

    // Verify the filename isn't NULL.
    if (filename == NULL)
    {
        printf("encrypt: Filename is NULL\n");
        return;
    }

    // Verify file is in directory
    int directory_entry = searchDirectory(filename);
	if (directory_entry == -1)
	{	
		printf("encrypt: File not found in directory.\n");
        return;
	}
    if (directory_ptr[directory_entry].in_use == 0)
    {
        printf("encrypt: File not found in directory.\n");
        return;
    }

    readFileRetrieve(filename, directory_entry);

    FILE *fpi = fopen("temp", "rb"); 
    if (fpi == NULL) 
    { 
        printf("encrypt: Can not open file.\n");
        return;
    }

    FILE *fpo = fopen(filename, "wb"); 
    if (fpo == NULL) 
    { 
        printf("encrypt: Can not open file.\n");
        return;
    }

    unsigned char block[1];
    int num_bytes;

    do {
		num_bytes = fread(block, 1, 1, fpi);

		for (int i = 0; i < num_bytes; i++) 
			block[i] ^= cipher;

		fwrite(block, 1, num_bytes, fpo);
	} while (num_bytes == 1);

    fclose(fpi);
    fclose(fpo);

    delete(filename);
    insert(filename);

    remove("temp");
}

// The decrypt command.
void decrypt(char *filename, char cipher)
{
    // Input: char *filename - The file we want to decrypt.
    //        char *cipher - The cipher used to decrypt the file.
    // Output: void. Decrypts the file using the given cipher.
    // Description: The data from the file in the the file system is put
    //              into the cwd. The file in the cwd is used to read the
    //              bytes of the file we want to decrypt. Each byte in
    //              the file is decrypted using the XOR cipher. 

    // Verify the filename isn't NULL.
    if (filename == NULL)
    {
        printf("decrypt: Filename is NULL\n");
        return;
    }

    // Verify file is in directory
    int directory_entry = searchDirectory(filename);
	if (directory_entry == -1)
	{	
		printf("decrypt: File not found in directory.\n");
        return;
	}
    if (directory_ptr[directory_entry].in_use == 0)
    {
        printf("decrypt: File not found in directory.\n");
        return;
    }

    readFileRetrieve(filename, directory_entry);

    FILE *fpi = fopen("temp", "rb"); 
    if (fpi == NULL) 
    { 
        printf("decrypt: Can not open file.\n");
        return;
    }

    FILE *fpo = fopen(filename, "wb"); 
    if (fpo == NULL) 
    { 
        printf("decrypt: Can not open file.\n");
        return;
    }

	unsigned char block[1];
    int num_bytes;

    do {
		num_bytes = fread(block, 1, 1, fpi);

		for (int i = 0; i < num_bytes; i++) 
			block[i] ^= cipher;

		fwrite(block, 1, num_bytes, fpo);
	} while (num_bytes == 1);

    fclose(fpi);
    fclose(fpo);

    delete(filename);
    insert(filename);

    remove("temp");
}

// Used to convert the hex value cipher given into a single decimal byte.
uint8_t hex_to_byte(char *hex) 
{
    // Input: char *hex - Hex value cipher.
    // Output: uint8_t. Returns the converted hex value cipher as a byte.
    // Description: strtol converts the hexadecimal string to a byte value.
    //              Value is checked to see if it is a byte value.
    //              Value is returned as an uint8_t.

    char* end_ptr;
    int value = strtol(hex, &end_ptr, 16);
    if (*end_ptr != '\0')
    {
        return -1;
    }

    if (value < 0 || value > 255)
    {
        return -1;
    }

    return (uint8_t)value;
}