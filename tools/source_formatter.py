#!/usr/bin/python
'''
    - Converts tabs to 4spaces
    - Make sure that newlines are '\n' ( so no Windows or Mac)
'''
import os
import sys

class Utility:
    @staticmethod
    def delete_file_if_exists(file_path):
        try:
            os.remove(file_path)
        except OSError:
            pass

    @staticmethod
    def save_to_file(file_name, text):
        Utility.delete_file_if_exists(file_name)
        
        with open(file_name, "w", newline='') as text_file:
            text_file.write(text)

total_number_of_lines = 0
            
def process_single_file(file_path, tab_size=4):
    global total_number_of_lines
    print("Processing " + file_path)
    
    content = ""
    spaces = ' ' * tab_size
    
    with open(file_path) as fp:
        lines = fp.readlines()
        for index, line in enumerate(lines):
            total_number_of_lines = total_number_of_lines +1
            # Remove new line 
            line = line.rstrip('\n\r')
            # Convert tabs to 4 spaces
            line = line.replace('\t', spaces)
            # Add to the content 
            content += line 
            if index != len(lines) - 1:
                content += "\n"
            
    Utility.delete_file_if_exists(file_path)
    Utility.save_to_file(file_path, content)
    
def display_usage():
    print('usage : python source_formatter.py <root_path>')

def main():
    try:
        if len(sys.argv) <2:
            display_usage()
            return
        
        extensions = ["c", "cpp", "h", "hpp", "inl", "cu"]
        search_path = sys.argv[1]
        # 1. FIND OUT FILES
        target_file_paths = []
        
        for root, dirs, files in os.walk(search_path):
            for file in files:
                # Check if the file has the desired extension
                if file.split('.')[-1] in extensions:
                    # Create full file path and add it to the list
                    target_file_paths.append(os.path.join(root, file))

        # 2. PROCESS EACH FILE
        for file_path in target_file_paths:
            process_single_file(file_path)
            
        print('Total number of lines = ' + str(total_number_of_lines))

    except ValueError as err:
        print(err.args)

#Entry point
if __name__ == "__main__":
    main()