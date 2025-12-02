# Specs

Tests are pulled from the main [try](https://github.com/tobi/try) Ruby repository.

## Running Tests

```bash
make test
```

This runs `get_specs.sh` to fetch specs, then executes them against the built binary.

## How It Works

The `get_specs.sh` script:

1. If `upstream` already exists, does nothing (allows local override)
2. Clones the try repo to `.try/` if not present
3. Pulls latest changes
4. Symlinks `upstream` -> `.try/spec`

## Local Override

To test against a local spec repo instead of upstream:

```bash
rm -f spec/upstream
ln -s /path/to/local/try/spec spec/upstream
```

The `get_specs.sh` script will skip fetching if `upstream` exists.
