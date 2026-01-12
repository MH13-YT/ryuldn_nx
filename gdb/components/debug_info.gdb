# Debug information commands
# Show memory regions and loaded sections

define show_sections
    echo \n===== Memory sections =====\n
    info files
    echo \n
end

define show_mappings
    echo \n===== Process mappings =====\n
    info proc mappings
    echo \n
end

define show_threads
    echo \n===== Threads =====\n
    info threads
    echo \n
end

define show_registers
    echo \n===== Registers =====\n
    info registers pc sp
    echo \n
end

define show_all_debug
    show_sections
    show_threads
    show_registers
end

document show_sections
Display all loaded sections and their addresses.
end

document show_mappings
Display process memory mappings.
end

document show_threads
Display all threads in the process.
end

document show_registers
Display program counter and stack pointer.
end

document show_all_debug
Display all debug information (sections, threads, registers).
end
