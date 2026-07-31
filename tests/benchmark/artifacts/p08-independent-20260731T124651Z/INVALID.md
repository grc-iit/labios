# Invalid Prompt 08 orchestration attempt

The first fresh-state cell completed and produced 47 verified rows, but the runner's seeded order CSV used CRLF line endings. The shell retained the carriage return in the repetition argument, so post-run identity validation compared `11` with `11\r` and stopped. No analysis was run and this bundle is not performance evidence.
