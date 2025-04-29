// Entry point of the program
// Author: Chris Cabang <chriscabang@outlook.com>
// Created: February 21, 2025
// Last modified: February 21, 2025
//
// ****************************************************************

#include <stdio.h>

// Prints the program's usage
// and version information

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: %s <filename>\n", argv[0]);
    return 1;
  }

  printf("Version 1.0\n");
  printf("Loading %s\n", argv[1]);

  return 0;
}
