# ==========================================
# Memory Debugging
# Track malloc, free, and heap operations
# ==========================================

# Catch memory allocation failures
break malloc
commands
silent
printf "[MEMORY] malloc(size=%d) requested\n", $arg0
bt 3
continue
end

break calloc
commands
silent
printf "[MEMORY] calloc(count=%d, size=%d) requested\n", $arg0, $arg1
continue
end

break realloc
commands
silent
printf "[MEMORY] realloc(ptr=0x%x, size=%d) requested\n", $arg0, $arg1
bt 3
continue
end

break free
commands
silent
printf "[MEMORY] free(ptr=0x%x) called\n", $arg0
continue
end

# Memory watch
define memory_check
  printf "Checking memory allocations...\n"
  info locals
  printf "Stack state captured\n"
end

echo [MEMORY] Memory tracking enabled with full logging\n
