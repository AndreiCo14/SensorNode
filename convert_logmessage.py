#!/usr/bin/env python3
"""
Script to convert logMessage calls to logMessageFmt format.
Handles patterns like:
  logMessage("text" + String(var) + "more", "level") 
  -> logMessageFmt("level", "text%dmore", var)
  
  logMessage("simple text", "level")
  -> logMessageFmt("level", "simple text")
"""

import re
import sys
import os

def convert_logmessage(content):
    """Convert logMessage calls to logMessageFmt format."""
    
    # Pattern 1: logMessage with String concatenation and level
    # logMessage("text" + String(var) + "more", "level")
    pattern1 = r'logMessage\(\s*"([^"]*)"\s*\+\s*String\(([^)]+)\)\s*\+\s*"([^"]*)"\s*,\s*"([^"]+)"\s*\)'
    
    def replace_pattern1(match):
        prefix = match.group(1)
        var = match.group(2).strip()
        suffix = match.group(3)
        level = match.group(4)
        
        # Determine format specifier based on variable name hints
        fmt_spec = "%d"
        if "HEX" in var or "_addr" in var.lower():
            fmt_spec = "0x%X"
            var = var.replace(", HEX", "").replace(",hex", "")
        elif "." in var or "temp" in var.lower() or "hum" in var.lower():
            fmt_spec = "%.2f"
        elif "size" in var.lower() or "count" in var.lower():
            fmt_spec = "%d"
        else:
            fmt_spec = "%s"
        
        format_str = prefix + fmt_spec + suffix
        return f'logMessageFmt("{level}", "{format_str}", {var})'
    
    content = re.sub(pattern1, replace_pattern1, content)
    
    # Pattern 2: logMessage with multiple String concatenations
    # logMessage(String("text") + var + "more", "level")
    pattern2 = r'logMessage\(\s*String\(\s*"([^"]*)"\s*\)\s*\+\s*([^,]+)\s*,\s*"([^"]+)"\s*\)'
    
    def replace_pattern2(match):
        prefix = match.group(1)
        var = match.group(2).strip()
        level = match.group(3)
        
        # Clean up variable
        var_clean = var.replace('.toString()', '')
        
        format_str = prefix + "%s"
        return f'logMessageFmt("{level}", "{format_str}", {var_clean})'
    
    content = re.sub(pattern2, replace_pattern2, content)
    
    # Pattern 3: logMessage with String(variable) only
    # logMessage(String(var), "level")
    pattern3 = r'logMessage\(\s*String\(([^)]+)\)\s*,\s*"([^"]+)"\s*\)'
    
    def replace_pattern3(match):
        var = match.group(1).strip()
        level = match.group(2)
        
        # Check for special formats
        if ", HEX" in var:
            var_clean = var.replace(", HEX", "").replace(",hex", "")
            return f'logMessageFmt("{level}", "0x%X", {var_clean})'
        else:
            var_clean = var.replace('.toString()', '')
            return f'logMessageFmt("{level}", "%s", {var_clean})'
    
    content = re.sub(pattern3, replace_pattern3, content)
    
    # Pattern 4: logMessage with simple string literal and level
    # logMessage("text", "level")
    pattern4 = r'logMessage\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)'
    
    def replace_pattern4(match):
        msg = match.group(1)
        level = match.group(2)
        return f'logMessageFmt("{level}", "{msg}")'
    
    content = re.sub(pattern4, replace_pattern4, content)
    
    # Pattern 5: logMessage with ternary operator
    # logMessage(ok ? "text1" : "text2", ok ? "info" : "error")
    # This is complex, leave it as logMessage for now or handle specially
    # For now, we'll skip these complex cases
    
    # Pattern 6: logMessage with single argument (default level)
    # logMessage("text")
    pattern6 = r'logMessage\(\s*"([^"]+)"\s*\)(?!\s*,)'
    
    def replace_pattern6(match):
        msg = match.group(1)
        return f'logMessageFmt("info", "{msg}")'
    
    content = re.sub(pattern6, replace_pattern6, content)
    
    return content

def process_file(filepath):
    """Process a single file."""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            original = f.read()
        
        converted = convert_logmessage(original)
        
        if original != converted:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(converted)
            print(f"✓ Converted: {filepath}")
            return True
        else:
            print(f"- No changes: {filepath}")
            return False
    except Exception as e:
        print(f"✗ Error processing {filepath}: {e}")
        return False

def main():
    if len(sys.argv) < 2:
        print("Usage: python convert_logmessage.py <file1> [file2] ...")
        print("   or: python convert_logmessage.py --all")
        sys.exit(1)
    
    files = []
    if sys.argv[1] == "--all":
        # Find all .cpp and .h files in src/
        for root, dirs, filenames in os.walk("src"):
            for filename in filenames:
                if filename.endswith('.cpp') or filename.endswith('.h'):
                    filepath = os.path.join(root, filename)
                    if 'logger.cpp' not in filepath and 'logger.h' not in filepath:
                        files.append(filepath)
    else:
        files = sys.argv[1:]
    
    count = 0
    for filepath in files:
        if process_file(filepath):
            count += 1
    
    print(f"\nTotal files converted: {count}")

if __name__ == "__main__":
    main()
