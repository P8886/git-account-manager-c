# Project Structure - Git Account Manager (C Version)

This document describes the purpose of each source file in the project.

## Core Logic & Entry Point
*   **`main.c`**: The application entry point. Handles the main window creation, message loop, UI event handling (buttons, lists), and integrates all other modules.
*   **`logic.c`**: Contains the core business logic, such as executing Git commands, managing SSH keys, and file operations.
*   **`logic.h`**: Header file for `logic.c`, exposing function prototypes.
*   **`shared.h`**: Defines common constants (e.g., buffer sizes), data structures (e.g., `Account`, `Config`), and global variable declarations used across multiple files.

## User Interface Modules
*   **`ui_draw.c`**: Implements custom UI drawing functions, specifically for rendering rounded buttons, input boxes, and handling high-DPI scaling or theme colors.
*   **`ui_draw.h`**: Header file for `ui_draw.c`.
*   **`ui_gen_key.c`**: Implements the "Generate SSH Key" modal dialog. Handles user input for key filename, email, and encryption type.
*   **`ui_gen_key.h`**: Header file for `ui_gen_key.c`.

## Resources & Build
*   **`resource.rc`**: Resource script file. Embeds the application manifest and other resources (like icons) into the executable.
*   **`GitManager.exe.manifest`**: Windows application manifest. Enables visual styles (Common Controls v6) and DPI awareness.
*   **`build.bat`**: Batch script to compile the project using GCC. It handles resource compilation and linking of all object files.

## Project Overview
This project is a lightweight, native Windows GUI application written in C (using the Win32 API) to manage and switch between multiple Git accounts (identities) globally. It avoids the overhead of Electron or other heavy frameworks.
