#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>

//*** data ***//

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};
#define CTRL_KEY(key) ((key) & 0x1f)

enum editorHighlight {
  HL_NORMAL = 0,
  HL_NUMBER
};

// Row object for file content
typedef struct erow {
    int size;
    char *chars;
    char *hl;
} erow;

// Buffer to hold volatile data
struct buf {
    char *b;
    int len;
};
#define BUF_INIT {NULL, 0}

struct editorConfig {
    struct termios termDefault;

    // Editor navigation
    int cx, cy;
    int screenrows, screencols;

    // File content
    int numrows;
    erow *row;
    int rowHl;
    int offsetY;
    int startX;

    // File and editing attributes
    char *filename;
    int insert;
    int readOnly;

    // Command input and prompt
    struct buf cmd;
    struct buf cmdSave;
    struct buf prompt;
};
struct editorConfig E;

//*** terminal ***//

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.termDefault);
}

void cleanUp() {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    disableRawMode();
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &E.termDefault);
    atexit(cleanUp);

    struct termios termRaw = E.termDefault;

    termRaw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    termRaw.c_oflag &= ~(0); // add OPOST if something not working
    termRaw.c_cflag |= (CS8);
    termRaw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    termRaw.c_cc[VMIN] = 0;
    termRaw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &termRaw);
}

//*** buffer ***//

void bufAppend(struct buf *ab, const char *s, int len) {

    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;

    memcpy(&new[ab->len], s, len);

    ab->b = new;
    ab->len += len;
}

void bufAppendChar(struct buf *ab, const char c) {

    char s[1];
    strcpy(s , (char[1]) { (char) c } );

    char *new = realloc(ab->b, ab->len + 1);

    if (new == NULL) return;

    memcpy(&new[ab->len], s, 1);

    ab->b = new;
    ab->len++;;
}

void bufFree(struct buf *ab) {
    ab->b = NULL;
    ab->len = 0;
}

//*** editor ***//

void moveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (E.cx > 1) {
                E.cx--;
            }
            else if (E.cy > 2) {
                E.cy--;
                E.cx = E.row[E.cy - 2 + E.offsetY].size + 1;
            }
            break;
        case ARROW_RIGHT:
            if (E.cx < E.screencols && E.cx < E.row[E.cy - 2 + E.offsetY].size + 1) {
                E.cx++;
            }
            else if (E.cx < E.screencols && E.cy + E.offsetY < E.numrows + 1 && E.cy < E.screenrows - 1) {
                E.cy++;
                E.cx = 1;
            }
            break;
        case ARROW_UP:
            if (E.cy > 2) {
                E.cy--;
                if (E.cx > E.row[E.cy - 2 + E.offsetY].size + 1) {
                    E.cx = E.row[E.cy - 2 + E.offsetY].size + 1;
                }
            }
            else if (E.cy == 2  && E.offsetY > 0) {
                E.offsetY--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows - E.offsetY + 1) {
                if (E.cy < E.screenrows - 5) {
                    int prev = (E.cx == E.row[E.cy - 2 + E.offsetY].size + 1);
                    E.cy++;
                    if (prev) E.cx = E.row[E.cy - 2 + E.offsetY].size + 1;
                    if (E.cx > E.row[E.cy - 2 + E.offsetY].size + 1) {
                        E.cx = E.row[E.cy - 2 + E.offsetY].size + 1;
                    }
                }
                else {
                    E.offsetY++;
                    if (E.cx > E.row[E.cy - 2 + E.offsetY].size + 1) {
                        E.cx = E.row[E.cy - 2 + E.offsetY].size + 1;
                    }
                }
                break;
            }
    }
}

void drawFileLine(struct buf *ab, int y) {
    int len = E.row[y-1 + E.offsetY].size;
    if (len > E.screencols) len = E.screencols;

    /* // Individual char print
    for (int i = 0; i <= E.row[y-1 + E.offsetY].size; i++) {
        int type = getType(E.row[y-1 + E.offsetY].chars[i], E.row[y-1 + E.offsetY].chars[i+1]);

        if (E.rowHl != COMMENT) {

            if (E.rowHl != STRING) {
                highlightToColor(ab, type);
            } else {
                if (type == STRING) {
                    bufAppend(ab, "\x1b[m", 3);
                    E.rowHl = type;
                }
            }
        
        }

        bufAppendChar(ab, E.row[y-1 + E.offsetY].chars[i]);
    }
    */
    
    // Entire line print:
    bufAppend(ab, E.row[y-1 + E.offsetY].chars, len);

    E.rowHl = 0;
    bufAppend(ab, "\x1b[m", 3);
    bufAppend(ab, "\r\n", 2);
}

void drawRows(struct buf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if ( y == 0 ) {

            char title[80];
            int len = snprintf(title, sizeof(title), "%.20s", E.filename ? E.filename : "[Unnamed Buffer]");
            
            bufAppend(ab, "\x1b[44m", 5);
            bufAppend(ab, title, len);
            while (len < E.screencols) {
                bufAppend(ab, " ", 1);
                len++;
            }
            bufAppend(ab, "\x1b[m\r\n", 5);
        }
        
        else if (y == E.screenrows-2 && E.prompt.b) {
            bufAppend(ab, "\x1b[45m", 5);
            bufAppend(ab, E.prompt.b, E.prompt.len);
            bufAppend(ab, "\x1b[m\r\n", 5);
        }
        else if (y == E.screenrows-1) {

            char info[80];
            int len = snprintf(info, sizeof(info), "%d lines  Ln %d, Col %d  Scl %d", E.numrows, E.cy, E.cx, E.offsetY);
            if (len > E.screencols) len = E.screencols;

            int i = len + (E.cmd.len != 0 ? E.cmd.len : 51);

            bufAppend(ab, "\x1b[44m", 5);
            bufAppend(ab, (E.cmd.len != 0 ? E.cmd.b : "Type 'help' in command mode (ESC) if you need help."), (E.cmd.len != 0 ? E.cmd.len : 51));
            while (i < E.screencols) {
                bufAppend(ab, " ", 1);
                i++;
            }
            bufAppend(ab, info, len);
            bufAppend(ab, "\x1b[m", 3);
        }
        
        else if ( y <= E.numrows - E.offsetY) {
            char nr[80];
            int len = snprintf(nr, sizeof(nr), "%d", y + E.offsetY);
            if (len > E.startX - 1) len = E.startX - 1;

            //bufAppend(ab, "\x1b[30m", 5);
            bufAppend(ab, nr, len);
            bufAppend(ab, "\x1b[m", 3);
            
            for(int i = len; i < E.startX - 1; i++) {
                bufAppend(ab, " ", 1);
            }
            drawFileLine(ab, y);
        }
        
        else {
            bufAppend(ab, (y < E.screenrows-1) ? "\033[36m~\033[0m\x1b[K\r\n" : "\033[36m~\033[0m", 17);
        }
        bufAppend(ab, "\x1b[K", 3);
    }
}

//*** file buffer content ***//

void insertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].hl = NULL;

    E.numrows++;
}

void delRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    free(E.row[at].chars);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--;
}

void rowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
}

void rowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
}

void rowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
}


void insertChar(int c) {
    rowInsertChar(&E.row[E.cy - 2 + E.offsetY], E.cx-1, c);
    E.cx++;
}

void insertNewline() {
    if (E.cx == E.row[E.cy - 2 + E.offsetY].size + 1) {
        insertRow(E.cy - 1 + E.offsetY, "", 0);
    } else {
        erow *row = &E.row[E.cy - 2 + E.offsetY];
        insertRow(E.cy - 1 + E.offsetY, &row->chars[E.cx - 1], row->size - E.cx + 1);
        row = &E.row[E.cy - 2 + E.offsetY];
        row->size = E.cx - 1;
        row->chars[row->size] = '\0';
    }
    if (E.cy + 1 >= E.screenrows - 5) {
        E.offsetY++;
    } else E.cy++;
    E.cx = 1;
}

void delChar() {
    if (E.cy - 2 + E.offsetY == E.numrows) return;
    if (E.cx == 1 && E.cy <= 2 && E.offsetY == 0) return;
    else if (E.cx == 1 && E.cy <= 2) { E.offsetY--; E.cy++; }

    erow *row = &E.row[E.cy - 2 + E.offsetY];
    if (E.cx > 1) {
        rowDelChar(row, E.cx - 2);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 3 + E.offsetY].size + 1;
        rowAppendString(&E.row[E.cy - 3 + E.offsetY], row->chars, row->size);
        delRow(E.cy - 2 + E.offsetY);
        E.cy--;
    }
}

//*** file ***//

void openFile(char *filename) {
    E.filename = filename;
    FILE *fp = fopen(filename, "r");
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
        insertRow(E.numrows, line, linelen);
    }

    free(line);
    fclose(fp);

    E.cx = 1; E.cy = 2;
    E.offsetY = 0;
    E.readOnly = 0;
}

void closeFile() {
    E.cx = 1; E.cy = 2;
    E.offsetY = 0;
    E.row = NULL;
    E.numrows = 0;
    E.filename = NULL;
}

void createFile() {
    insertRow(0, "", 0);
    E.readOnly = 0;
}

char *rowsToString(int *buflen) {
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;
    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void save() {
    if (E.filename == NULL) E.filename = "unnamed";
    int len;
    char *buf = rowsToString(&len);
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    ftruncate(fd, len);
    write(fd, buf, len);
    close(fd);
    free(buf);
}

//*** operation modes ***//

int saveX, saveY;
void setInsert(int posX, int posY ) {
    E.cx = posX; E.cy = posY;
    bufFree(&E.cmd);
    E.insert = 1;
}
void unsetInsert() {
    E.insert = 0;
    saveX = E.cx; saveY = E.cy;
    E.cx = 1; E.cy = E.screenrows;
}

//*** commands ***//

void print(char *str) {
    bufAppend(&E.prompt, str, strlen(str));
}

void openCommand(char *arg) {
    if (access(arg, F_OK) == 0) {
        closeFile();
        openFile(arg);
        saveX = 1; saveY = 2;
        setInsert(saveX, saveY);
    } else bufAppend(&E.prompt, "Error: File does not exist.", 21);
}

void closeCommand() {
    closeFile();
    createFile();
    saveX = 1; saveY = 2;
    setInsert(saveX, saveY);
}

void renameCommand(char *arg) {
    E.filename = arg;
}

void saveCommand(char *arg) {
    if(E.readOnly) {
        bufAppend(&E.prompt, "Error: This file is read-only!", 24);
        return;
    }
    if(arg) E.filename = arg;
    save();
}

void moveCommand() {
    print("Cannot move yet.");
} 

void startCommand() {
    setInsert(1, E.insert ? E.cy : saveY);
}

void endCommand() {
    setInsert((E.row[E.cy - 2 + E.offsetY].size + 1), E.insert ? E.cy : saveY);
}

void topCommand() {
    E.offsetY = 0;
    setInsert(saveX, saveY);
}

void bottomCommand() {
    if (E.numrows > E.screenrows) E.offsetY = E.numrows - E.screenrows + 2;
    setInsert(saveX, saveY);
}

void upCommand() {
    if (E.offsetY > 5) E.offsetY-=5;
    else E.offsetY = 0;

    if (E.cx > E.row[E.cy - 2 + E.offsetY].size + 1) {
        E.cx = E.row[E.cy - 2 + E.offsetY].size + 1;
    }
    setInsert(saveX, saveY);
}

void downCommand() {
    if (E.numrows > E.offsetY + E.screenrows) {
        E.offsetY += 5;    
    }

    if (E.cx > E.row[E.cy - 2 + E.offsetY].size + 1) {
        E.cx = E.row[E.cy - 2 + E.offsetY].size + 1;
    }
    setInsert(saveX, saveY);
}

void gotoCommand(char *arg) {
    int line = atoi(arg);
    E.cx = 1; E.cy = 2;
    E.offsetY = line - 1;
    setInsert(saveX, saveY);
}

void helpCommand() {
    closeFile();
    openFile("./help.txt");
    E.readOnly = 1;
    saveX = 1; saveY = 2;
    setInsert(saveX, saveY);
}

//*** user input ***//

enum commands {
    OPEN = 0,
    CLOSE,
    RENAME,
    SAVE,
    EXIT,
    MOVE,
    START_LINE, // Move to start of line
    END_LINE, // Move to end of line
    SCRN_UP,
    SCRN_DOWN,
    TOP, // Move cursor to first position in file
    BOTTOM, // Move screen to end of file
    GOTO,
    HELP
};

int getCommand( char *c) {
    if(!strcmp(c, "open") || !strcmp(c, "edit"))
        return OPEN;
    else if(!strcmp(c, "close") || !strcmp(c, "new") || !strcmp(c, "create"))
        return CLOSE;
    else if(!strcmp(c, "rename") || !strcmp(c, "name"))
        return RENAME;
    else if(!strcmp(c, "save"))
        return SAVE;
    else if(!strcmp(c, "exit") || !strcmp(c, "quit") || !strcmp(c, "leave"))
        return EXIT;
    else if(!strcmp(c, "move") || !strcmp(c, "mv"))
        return MOVE;
    else if(!strcmp(c, "start"))
        return START_LINE;
    else if(!strcmp(c, "end"))
        return END_LINE;
    else if(!strcmp(c, "up"))
        return SCRN_UP;
    else if(!strcmp(c, "down"))
        return SCRN_DOWN;
    else if(!strcmp(c, "top"))
        return TOP;
    else if(!strcmp(c, "bottom"))
        return BOTTOM;
    else if(!strcmp(c, "goto"))
        return GOTO;
    else if(!strcmp(c, "help"))
        return HELP;
    else return 1000;

}

int readKey() {
    char c;
    while ((read(STDIN_FILENO, &c, 1)) != 1) {}
    
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

void processKeypress() {
    int c = readKey();
    bufFree(&E.prompt);

    if (E.insert) {
        switch (c) {
            case '\r':
                if (!E.readOnly) insertNewline(); else bufAppend(&E.prompt, "This file is read-only!", 24);
                break;

            case BACKSPACE:
            case DEL_KEY:
                if (!E.readOnly) {
                    if (c == DEL_KEY) moveCursor(ARROW_RIGHT);
                    delChar();
                } else bufAppend(&E.prompt, "This file is read-only!", 24);
                break;

            case '\x1b':
                unsetInsert();
                break;

            case CTRL_KEY('q'):
                exit(0);

            case CTRL_KEY('s'):
                saveCommand(NULL);
                print("Success: File saved.");
                break;

            case CTRL_KEY('t'):
                topCommand();
                break;

            case CTRL_KEY('b'):
                bottomCommand();
                break;

            case ARROW_UP:
            case ARROW_DOWN:
            case ARROW_LEFT:
            case ARROW_RIGHT:
                moveCursor(c);
                break;

            case HOME_KEY:
                startCommand();
                break;

            case END_KEY:
                endCommand();
                break;

            case PAGE_UP:
            case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    upCommand();
                } else if (c == PAGE_DOWN) {
                    downCommand();
                }
            }
            break;

            default:
                if (!E.readOnly) insertChar(c); else bufAppend(&E.prompt, "This file is read-only!", 24);
                break;
        }
    } else {
        if (c == CTRL_KEY('q')) exit(0);
        else if (c == CTRL_KEY('s')) { saveCommand(NULL); print("Success: File saved."); }
        else if (c == '\x1b') { setInsert(saveX, saveY); }
        else if (c == ARROW_UP) {
            E.cmd = E.cmdSave;
            E.cx = E.cmd.len + 1;
        }
        else if (c == ARROW_DOWN) {
            bufFree(&E.cmd);
            E.cx = 1;
        }
        else if (c == BACKSPACE && E.cx > 1) {
            E.cmd.len--;
            E.cx--;
        }
        
        else if (c == '\r') {
            char *command = strtok(E.cmd.b, " ");
            char *arg1;
            arg1 = strtok(NULL, " ");

            switch (getCommand(command)) {
                case OPEN:
                    if (arg1) {
                        openCommand(arg1);
                        print("Success: New file opened.");
                    } else print("Invalid option: No file path specified.");
                    break;
                case CLOSE:
                    closeCommand();
                    print("Success: File closed.");
                    break;
                case RENAME:
                    if(arg1) {
                        renameCommand(arg1);
                        print("Success: File renamed.");
                    } else print("Invalid option: No file name specified. No changes made.");
                    break;
                case SAVE:
                    saveCommand(arg1);
                    if (arg1) print("Success: File renamed and saved."); else print("Success: File saved.");
                    break;
                case EXIT:
                    exit(0);
                    break;
                case MOVE:
                    if (arg1) {
                        moveCommand();
                    } else print("Invalid option: No position specified.");
                    break;
                case START_LINE:
                    startCommand();
                    break;
                case END_LINE:
                    endCommand();
                    break;
                case SCRN_UP:
                    saveX = E.cx; saveY = E.cy;
                    upCommand();
                    break;
                case SCRN_DOWN:
                    saveX = E.cx; saveY = E.cy;
                    downCommand();
                    break;
                case TOP:
                    topCommand();
                    break;
                case BOTTOM:
                    bottomCommand();
                    break;
                case GOTO:
                    if (arg1)
                        gotoCommand(arg1);
                    else if (arg1) print("Invalid option: Line number outside of file range.");
                    else print("Invalid option: No line number specified.");
                    break;
                case HELP:
                    helpCommand();
                    break;
                default:
                    print("Invalid command: Command not recognized.");
                    break;
            }

            E.cmdSave = E.cmd;
            bufFree(&E.cmd);
            E.cx = 1;
        }
        else if (c != BACKSPACE) {
            char buffer = c;
            bufAppend(&E.cmd, &buffer, 1);
            E.cx++;
        }
    }
}

//*** core control ***//

void initEditor() {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    getWindowSize(&E.screenrows, &E.screencols);

    E.cx = 1; E.cy = 2;
    E.offsetY = 0;
    E.startX = 7;
    E.numrows = 0;
    E.row = NULL;
    E.rowHl = 0;
    E.filename = NULL;
    E.insert = 1;
    E.cmd.b = NULL; E.cmd.len = 0;
    E.prompt.b = NULL; E.prompt.len = 0;

    E.readOnly = 0;
}

void refreshEditor() {
    struct buf ab = BUF_INIT;

    bufAppend(&ab, "\x1b[?25l", 6); // Hide cursor
    bufAppend(&ab, "\x1b[H", 3); // Move cursor to 1,1

    drawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy, E.cx + (E.insert ? E.startX - 1 : 0));
    bufAppend(&ab, buf, strlen(buf));
    bufAppend(&ab, "\x1b[?25h", 6); // Show cursor

    write(STDOUT_FILENO, ab.b, ab.len);
    bufFree(&ab);
}

int main(int argc, char *argv[]) {
    enableRawMode();

    initEditor();
    
    if (argc >= 2) {
        openFile(argv[1]);
    }
    else createFile();

    while (1) {
        refreshEditor();
        processKeypress();
    }
}
