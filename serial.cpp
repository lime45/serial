// On Windows under cygwin, try /dev/comNN, where NN is 0, 1, 2, etc
// GUC-232A came up as /dev/com4 just now (I think)


#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <stdlib.h>
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <errno.h>   /* Error number definitions */
#include <sys/types.h>
#include <sys/time.h>

unsigned char buf[512];

unsigned baudMapping[][2] = {
  {0  , B0  },
  {50  , B50  },
  {75  , B75  },
  {110  , B110  },
  {134  , B134  },
  {150  , B150  },
  {200  , B200  },
  {300  , B300  },
  {600  , B600  },
  {1200  , B1200  },
  {1800  , B1800  },
  {2400  , B2400  },
  {4800  , B4800  },
  {9600  , B9600  },
  {19200  , B19200  },
  {38400  , B38400  },
  {57600  , B57600  },
  {115200  , B115200  },
  // {128000  , B128000  },
  {230400  , B230400  },
  // {256000  , B256000  },
};
int baudMappingCount = sizeof(baudMapping) / sizeof(baudMapping[0]);

char presetNames[10][512];
char presetStrings[10][16384]; // should probably use std::string

void usage(char *progname)
{
  fprintf(stderr, "\n");
  fprintf(stderr, "serial v1.0 by Brad Grantham, grantham@plunk.org\n\n");
  fprintf(stderr, "with a minor fix by Chris Browning\n\n");
  fprintf(stderr, "usage: %s serialportfile baud\n", progname);
  fprintf(stderr, "e.g.: %s /dev/ttyS0 38400\n", progname);
  fprintf(stderr, "\n\nThe file $HOME/.serial (%s/.serial in your specific case) can also\n", getenv("HOME"));
  fprintf(stderr, "contain 10 string presets which are emitted when pressing \"~\" (tilde)\n");
  fprintf(stderr, "followed by one of the keys \"1\" through \"0\".\n");
  fprintf(stderr, "This file contains one preset per line, of the format:\n");
  fprintf(stderr, "\n    name-of-preset preset-string-here\n");
  fprintf(stderr, "\nThe preset string itself can contain spaces and also can contain embedded\n");
  fprintf(stderr, "newlines in the form \"\\n\".  Here's a short example file:\n");
  fprintf(stderr, "\n    restart-device reboot\\n\n");
  fprintf(stderr, "    initiate-connection telnet distant-machine\\nexport DISPLAY=flebbenge:0\\n\n");
  fprintf(stderr, "\nPressing \"~\" then 1 will send \"reboot\" and a newline over the serial port.\n");
  fprintf(stderr, "Pressing \"~\" then 2 will send \"telnet distant-machine\" over the serial\n");
  fprintf(stderr, "port, then a newline, then \"export DISPLAY=flebbenge:0\", and then\n");
  fprintf(stderr, "another newline.\n");
  fprintf(stderr, "When running, press \"~\" (tilde) and then \"h\" for some help.\n");
  fprintf(stderr, "\n");
}

int main(int argc, char **argv)
{
  int             speed;
  int             duplex = 0, crnl = 0;
  int             serial;
  int		    tty_in, tty_out;
  struct termios  options; 
  struct termios  old_termios; 
  unsigned int    baud;
  char            stringbuf[16384];
  bool	    done = false;
  fd_set	    reads;
  struct timeval  timeout;

  bool saw_tilde = false;

  FILE *presetFile;
  char presetName[512];

  for(int i = 0; i < 10; i++)
    presetStrings[i][0] = '\0';

  sprintf(presetName, "%s/.serial", getenv("HOME"));
  presetFile = fopen(presetName, "r");
  if(presetFile == NULL) {
    fprintf(stderr, "couldn't open preset strings file \"%s\"\n", presetName);
    fprintf(stderr, "proceeding without preset strings.\n");
  } else {
    for(int i = 0; i < 10; i++) {
      int which = (i + 1) % 10;

      if(fscanf(presetFile, "%s ", presetNames[which]) != 1)
        break;

      if(fgets(stringbuf, sizeof(stringbuf) - 1, presetFile) == NULL) {
        fprintf(stderr, "preset for %d (\"%s\") had a name but no string.  Ignored.\n", which, presetNames[which]);
        break;
      }
      stringbuf[strlen(stringbuf) - 1] = '\0';

      char *dst = presetStrings[which], *src = stringbuf;
      while(*src) {
        if(src[0] == '\\' && src[1] == 'n') {
          *dst++ = '\n';
          src += 2;
        } else
          *dst++ = *src++;
      }
      *dst++ = '\0';
    }
    fclose(presetFile);
  }

  if(argc < 3) {
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  if(argv[2][0] < '0' || argv[2][0] > '9') {
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  baud = (unsigned int) atoi(argv[2]);
  int which;
  for(which = 0; which < baudMappingCount; which++){
    if(baudMapping[which][0] == baud) {
      baud = baudMapping[which][1];
      break;
    }
  }
  if(which == baudMappingCount) {
    fprintf(stderr, "Didn't understand baud rate \"%s\"\n", argv[2]);
    exit(EXIT_FAILURE);
  }
  if(false) printf("baud mapping chosen: %d (0x%X)\n", baud, baud);

  tty_in = dup(0);
  if(tty_in == -1) {
    fprintf(stderr, "Can't open dup of stdin\n");
    exit(EXIT_FAILURE);
  }

  if(fcntl(tty_in, F_SETFL, O_NONBLOCK) == -1) {
    fprintf(stderr, "Failed to set nonblocking stdin\n");
    exit(EXIT_FAILURE);
  }

  tty_out = dup(1);
  if(tty_out == -1) {
    fprintf(stderr, "Can't open dup of stdout\n");
    exit(EXIT_FAILURE);
  }

  serial = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK);
  if(serial == -1) {
    fprintf(stderr, "Can't open serial port \"%s\"\n", argv[1]);
    exit(EXIT_FAILURE);
  }

#if 0
  if(fcntl(serial, F_SETFL, 0) == -1) {
    fprintf(stderr, "Failed to reset fcntl on serial\n");
    exit(EXIT_FAILURE);
  }
#endif

  /*
   * get the current options 
   */
  tcgetattr(tty_in, &old_termios);
  tcgetattr(tty_in, &options);

  /*
   * set raw input, 1 second timeout 
   */
  options.c_cflag |= (CLOCAL | CREAD);
  options.c_cc[VMIN] = 0;
  options.c_cc[VTIME] = 10;

  options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  options.c_iflag &= ~INLCR;
  options.c_iflag &= ~ICRNL;

  /*
   * Miscellaneous stuff 
   */
  options.c_cflag |= (CLOCAL | CREAD);	/* Enable receiver, set
                                        * local */
  /*
   * Linux seems to have problem with the following ??!! 
   */
  options.c_cflag |= (IXON | IXOFF);	/* Software flow control */
  options.c_lflag = 0;	/* no local flags */
  options.c_cflag |= HUPCL;	/* Drop DTR on close */

  /*
   * Clear the line 
   */
  tcflush(tty_in, TCIFLUSH);

  /*
   * Update the options synchronously 
   */
  if (tcsetattr(tty_in, TCSANOW, &options) != 0) {
    perror("setting stdin tc");
    goto restore;
  }


  /*
   * get the current options 
   */
  tcgetattr(serial, &options);

  /*
   * set raw input, 1 second timeout 
   */
  options.c_cflag |= (CLOCAL | CREAD);
  options.c_oflag &= ~OPOST;
  options.c_cc[VMIN] = 0;
  options.c_cc[VTIME] = 10;


  options.c_iflag &= ~INPCK;	/* Enable parity checking */
  options.c_iflag |= IGNPAR;

  options.c_cflag &= ~PARENB;	/* Clear parity enable */
  options.c_cflag &= ~CSTOPB;
  options.c_cflag &= ~CSIZE;
  options.c_cflag |= CS8;

  options.c_cflag &= ~CRTSCTS;

  // options.c_iflag &= ~(IXON | IXOFF | IXANY); /* no flow control */

  options.c_oflag &= ~(IXON | IXOFF | IXANY);	/* no flow control */

  options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  options.c_oflag &= ~OPOST;	/* No output processing */
  options.c_iflag &= ~INLCR;	/* Don't convert linefeeds */
  options.c_iflag &= ~ICRNL;	/* Don't convert linefeeds */

  /*
   * Miscellaneous stuff 
   */
  options.c_cflag |= (CLOCAL | CREAD);	/* Enable receiver, set
                                        * local */
  /*
   * Linux seems to have problem with the following ??!! 
   */
  options.c_cflag |= (IXON | IXOFF);	/* Software flow control */
  options.c_lflag = 0;	/* no local flags */
  options.c_cflag |= HUPCL;	/* Drop DTR on close */

  cfsetispeed(&options, baud);
  speed = cfgetispeed(&options);
  if(false) printf("set tty input to speed %d, expected %d\n", speed, baud);
  cfsetospeed(&options, baud);
  speed = cfgetospeed(&options);
  if(false) printf("set tty output to speed %d, expected %d\n", speed, baud);

  /*
   * Clear the line 
   */
  tcflush(serial, TCIFLUSH);

  if (tcsetattr(serial, TCSANOW, &options) != 0) {
    perror("setting serial tc");
    goto restore;
  }
  tcflush(serial, TCIFLUSH);


  printf("press \"~\" (tilde) and then \"h\" for some help.\n");

  while(!done) {

    FD_ZERO(&reads);
    FD_SET(serial, &reads);
    FD_SET(tty_in, &reads);

    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;

    int result = select(FD_SETSIZE, &reads, NULL, NULL, &timeout);

    if(result < 0) {

      perror("select");
      done = true;
      continue;

    } else if(result == 0) {

      if(false) printf("select timed out.\n");

    } else { 

      for(int i = 0 ; i < FD_SETSIZE; i++) {
        if(FD_ISSET(i, &reads)) {
          if(false) printf("read on %d\n", i);
        }
      }

      if(FD_ISSET(serial, &reads)) {
        if(false) printf("Read from serial\n");

        int byte_count = read(serial, buf, sizeof(buf));

        if(byte_count == 0) {
          fprintf(stderr, "unknown read of 0 bytes!\n");
          done = true;
          continue;
        }

        write(tty_out, buf, byte_count);
      }

      if(FD_ISSET(tty_in, &reads)) {
        if(false) printf("Read from TTY\n");

        int byte_count = read(tty_in, buf, sizeof(buf));

        if(saw_tilde) {
          if(buf[0] == 'h' || buf[0] == '?') {
            printf("1\n");
            printf("key help:\n");
            printf("    .   - exit\n");
            printf("    d   - toggle duplex\n");
            printf("    n   - toggle whether to send CR with NL\n");
            printf("    0-9 - send preset strings from ~/.serial\n");
            int i;
            for(i = 0; i < 10; i++) {
              int which = (i + 1) % 10;
              if(presetStrings[which][0] == '\0')
                break;
              printf("        %d : \"%s\"\n", which, presetNames[which]);
            }
            if(i == 0)
              printf("        (no preset strings)\n");
            printf("    p   - print contents of presets\n");
            saw_tilde = false;
            continue;
          } else if(buf[0] >= '0' && buf[0] <= '9') {
            int which = buf[0] - '0';
            write(serial, presetStrings[which], strlen(presetStrings[which]));
            saw_tilde = false;
            continue;

          } else if(buf[0] == 'p') {
            printf("preset strings from ~/.serial:\n");
            int i;
            for(i = 0; i < 10; i++) {
              int which = (i + 1) % 10;
              if(presetStrings[which][0] == '\0')
                break;
              printf("  %d, \"%15s\",  : \"%s\"\n", which, presetNames[which], presetStrings[which]);
            }
            if(i == 0)
              printf("  (no preset strings)\n");
            saw_tilde = false;
            continue;
          } else if(buf[0] == '.') {
            done = true;
            saw_tilde = false;
            continue;
          } else if(buf[0] == 'd') {
            duplex = !duplex;
            saw_tilde = false;
            continue;
          } else if(buf[0] == 'n') {

            crnl = !crnl;

#if 1
            tcgetattr(serial, &options);
            if(crnl)
              options.c_iflag |= ICRNL;
            else
              options.c_iflag &= ~ICRNL;
            tcsetattr(serial, TCSANOW, &options);
#endif

            tcgetattr(tty_out, &options);
            if(crnl)
              options.c_oflag |= OCRNL;
            else
              options.c_oflag &= ~OCRNL;
            tcsetattr(tty_out, TCSANOW, &options);
            continue;
            saw_tilde = false;
          }
          else saw_tilde = false;
        } else if(buf[0] == '~') {
          saw_tilde = true;
          continue; /* ick */
        } else {
          saw_tilde = false;
        }

        if(byte_count == 0) {
          fprintf(stderr, "unknown read of 0 bytes!\n");
          done = true;
          continue;
        }

        if(false) printf("writing %d bytes: '%c', %d\n", byte_count, buf[0], buf[0]);
        write(serial, buf, byte_count);

        if(duplex)
          write(tty_out, buf, byte_count);
      }
    }
  }

restore:
  if (tcsetattr(tty_in, TCSANOW, &old_termios) != 0) {
    perror("restoring stdin");
    return (0);
  }
  close(serial);
  close(tty_in);
  close(tty_out);

  return 0;
}
