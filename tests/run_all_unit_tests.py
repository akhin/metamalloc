#!/usr/bin/python
import os
import time
import shutil
import glob
from pathlib import Path
from sys import platform as _platform

class Utility:
    CONSOLE_RED = '\033[91m'
    CONSOLE_BLUE = '\033[94m'
    CONSOLE_YELLOW = '\033[93m'
    CONSOLE_END = '\033[0m'
    CONSOLE_GREEN = '\033[92m'

    @staticmethod
    def get_file_content(file_name):
        ret = ""
        if Utility.is_valid_file(file_name):
            ret = open(file_name, 'r').read()
        return ret
        
    @staticmethod
    def is_valid_file(file_path):
        return os.path.isfile(file_path)

    @staticmethod
    def write_colour_message(message, colour_code):
        if _platform == "linux" or _platform == "linux2":
            print(colour_code + message + Utility.CONSOLE_END)
        elif _platform == "win32":
            os.system("echo " + message)

    @staticmethod
    def execute_shell_command(command):
        os.system(command)

def delete_file_or_folder_if_exists(path):
    try:
        if os.path.isfile(path):
            os.remove(path)
        elif os.path.isdir(path):
            shutil.rmtree(path)
    except OSError as e:
        print(f"Error: {e}")

def delete_files_with_pattern(pattern):
    files = glob.glob(pattern)
    for file_path in files:
        try:
            if os.path.isfile(file_path):
                os.remove(file_path)
        except OSError as e:
            print(f"Error deleting {file_path}: {e}")

def perform_single_unit_test(unit_test_path):
    print("")
    Utility.write_colour_message("Performing test on folder " + unit_test_path, Utility.CONSOLE_BLUE)
    print("")
    # 1. CHANGE WORKING DIRECTORY
    os.chdir(unit_test_path)
    # 2. CLEAN BINARIES AND SERIALISATIONS
    delete_files_with_pattern("unit_test*.exe")
    delete_file_or_folder_if_exists(".vs")
    delete_file_or_folder_if_exists("x64")
    delete_file_or_folder_if_exists("log.txt")
    delete_file_or_folder_if_exists("results.txt")
    delete_file_or_folder_if_exists("sequence.store")
    delete_file_or_folder_if_exists("orders")
    delete_file_or_folder_if_exists("messages_incoming")
    delete_file_or_folder_if_exists("messages_outgoing")
    # 2. BUILD
    if _platform == "linux" or _platform == "linux2":
        Utility.execute_shell_command("make clean")
        Utility.execute_shell_command("make debug")
    elif _platform == "win32":
        Utility.execute_shell_command("build_msvc.bat no_pause")
    # 3. RUN WITH >> result.txt
    full_path = Path(unit_test_path)
    executable_command = full_path.name
    if _platform == "win32":
        executable_command = executable_command + ".exe no_pause"
    elif _platform == "linux" or _platform == "linux2":
        executable_command = "./" + executable_command
    Utility.execute_shell_command(executable_command + " >> results.txt")

def display_test_result(unit_test_path):
    # 1. CHANGE WORKING DIRECTORY
    os.chdir(unit_test_path)
    # 2. CHECK IF result,txt EXISTS , IF NOT OR EMPTY TEST HAS FAILED
    if Utility.is_valid_file("./results.txt") is False:
        print("")
        Utility.write_colour_message("Tests in " + unit_test_path + " failed !!!", Utility.CONSOLE_RED)
        print("")
        return
    if os.stat("./results.txt").st_size == 0:
        print("")
        Utility.write_colour_message("Tests in " + unit_test_path + " failed !!!", Utility.CONSOLE_RED)
        print("")
        return
    # 3. CHECK ERRORS in result.txt
    content = Utility.get_file_content("./results.txt")
    if "Failed test case number : 0" not in content:
        print("")
        Utility.write_colour_message("Tests in " + unit_test_path + " failed !!!", Utility.CONSOLE_RED)
        print("")
        return

    Utility.write_colour_message("Tests in " + unit_test_path + " passed", Utility.CONSOLE_GREEN)

def main():
    try:
        start_time = time.time()

        # 1. FIND OUT UNIT TEST FOLDERS
        unit_test_folders = []
        search_path = "."
        for entry in os.listdir(search_path):
            full_path = os.path.join(search_path, entry)
            full_path = os.path.abspath(full_path)
            if "unit_test" in full_path:
                if os.path.isdir(full_path):
                    unit_test_folders.append(full_path)

        # 2. FOR EACH UNIT TEST FOLDER , PERFORM THE TEST
        for unit_test_path in unit_test_folders:
            perform_single_unit_test(unit_test_path)

        # 3. FOR EACH UNIT TEST FOLDER , CHECK IF ANY HAS ERRORS
        print("")
        for unit_test_path in unit_test_folders:
            display_test_result(unit_test_path)

        # DISPLAY ELAPSED TIME
        end_time = time.time()
        elapsed_time = end_time - start_time
        minutes = int(elapsed_time // 60)
        seconds = int(elapsed_time % 60)
        print("")
        print(f"Elapsed time: {minutes} minutes and {seconds} seconds")
        print("")

    except ValueError as err:
        print(err.args)

#Entry point
if __name__ == "__main__":
    main()