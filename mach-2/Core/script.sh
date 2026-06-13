
#!/usr/bin/env bash

find . \( -name "*.c" -o -name "*.h" \) -type f | sort | while read -r file; do
    echo "### FILE: $file"
    echo '```c'
    cat "$file"
    echo
    echo '```'
    echo
done
