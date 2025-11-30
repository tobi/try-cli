#define _POSIX_C_SOURCE 200809L
#include "utils.h"
#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

zstr zstr_expand_tokens(const char *text) {
  zstr buffer = zstr_init();
  const char *in = text;

  while (*in) {
    if (*in == '{') {
      if (strncmp(in, "{h1}", 4) == 0) {
        zstr_cat(&buffer, ANSI_BOLD);
        zstr_cat(&buffer, "\033[38;5;214m"); // Orange
        in += 4;
      } else if (strncmp(in, "{h2}", 4) == 0) {
        zstr_cat(&buffer, ANSI_BOLD);
        zstr_cat(&buffer, ANSI_BLUE);
        in += 4;
      } else if (strncmp(in, "{b}", 3) == 0) {
        zstr_cat(&buffer, ANSI_BOLD);
        zstr_cat(&buffer, ANSI_YELLOW);
        in += 3;
      } else if (strncmp(in, "{/b}", 4) == 0) {
        zstr_cat(&buffer, "\033[22m"); // Turn off bold
        in += 4;
      } else if (strncmp(in, "{dim}", 5) == 0) {
        zstr_cat(&buffer, ANSI_DIM);
        in += 5;
      } else if (strncmp(in, "{reset}", 7) == 0) {
        zstr_cat(&buffer, ANSI_RESET);
        in += 7;
      } else if (strncmp(in, "{/fg}", 5) == 0) {
        zstr_cat(&buffer, "\033[39m"); // Reset foreground
        in += 5;
      } else if (strncmp(in, "{text}", 6) == 0) {
        zstr_cat(&buffer, ANSI_RESET);
        in += 6;
      } else if (strncmp(in, "{section}", 9) == 0) {
        zstr_cat(&buffer, ANSI_BOLD);
        in += 9;
      } else if (strncmp(in, "{/section}", 10) == 0) {
        zstr_cat(&buffer, ANSI_RESET);
        in += 10;
      } else {
        zstr_push(&buffer, *in++);
      }
    } else {
      zstr_push(&buffer, *in++);
    }
  }
  return buffer;
}

char *trim(char *str) {
  char *end;
  while (isspace((unsigned char)*str))
    str++;
  if (*str == 0)
    return str;
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
    end--;
  end[1] = '\0';
  return str;
}

zstr get_home_dir() {
  const char *home = getenv("HOME");
  if (!home) {
    struct passwd *pw = getpwuid(getuid());
    if (pw)
      home = pw->pw_dir;
  }
  return home ? zstr_from(home) : zstr_init();
}

zstr join_path(const char *dir, const char *file) {
  zstr s = zstr_from(dir);
  zstr_cat(&s, "/");
  zstr_cat(&s, file);
  return s;
}

zstr get_default_tries_path() {
  Z_CLEANUP(zstr_free) zstr home = get_home_dir();
  if (zstr_is_empty(&home))
    return home;
  zstr path = join_path(zstr_cstr(&home), DEFAULT_TRIES_PATH_SUFFIX);
  zstr_free(&home);
  return path;
}

bool dir_exists(const char *path) {
  struct stat sb;
  return (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode));
}

bool file_exists(const char *path) {
  struct stat sb;
  return (stat(path, &sb) == 0 && S_ISREG(sb.st_mode));
}

int mkdir_p(const char *path) {
  // System call is easiest for mkdir -p equivalent in portable C without
  // libraries But let's try to do it manually or just use system("mkdir -p
  // ...") for simplicity in this task? The requirement is "dependency free
  // modern c project". calling system() is standard C. However, implementing it
  // is better.

  char tmp[1024];
  char *p = NULL;
  size_t len;

  snprintf(tmp, sizeof(tmp), "%s", path);
  len = strlen(tmp);
  if (tmp[len - 1] == '/')
    tmp[len - 1] = 0;

  for (p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      if (mkdir(tmp, S_IRWXU) != 0 && errno != EEXIST) {
        return -1;
      }
      *p = '/';
    }
  }
  if (mkdir(tmp, S_IRWXU) != 0 && errno != EEXIST) {
    return -1;
  }
  return 0;
}

zstr format_relative_time(time_t mtime) {
  time_t now = time(NULL);
  double diff = difftime(now, mtime);
  zstr s = zstr_init();

  if (diff < 60) {
    zstr_cat(&s, "just now");
  } else if (diff < 3600) {
    zstr_fmt(&s, "%dm ago", (int)(diff / 60));
  } else if (diff < 86400) {
    zstr_fmt(&s, "%dh ago", (int)(diff / 3600));
  } else {
    zstr_fmt(&s, "%dd ago", (int)(diff / 86400));
  }
  return s;
}
