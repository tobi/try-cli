# try - fresh directories for every vibe

*Your experiments deserve a home.* 🏠

> **C implementation** of [try](https://github.com/tobi/try) - blazing fast, zero dependencies, same philosophy
>
> *Prefer Ruby? Use the [original Ruby version](https://github.com/tobi/try) - it's excellent!*

Ever find yourself with 50 directories named `test`, `test2`, `new-test`, `actually-working-test`, scattered across your filesystem? Or worse, just coding in `/tmp` and losing everything?

**try** is here for your beautifully chaotic mind.

# What it does

![Fuzzy Search Demo](assets/try-fuzzy-search-demo.gif)

Instantly navigate through all your experiment directories with:
- **Fuzzy search** that just works
- **Smart sorting** - recently used stuff bubbles to the top
- **Auto-dating** - creates directories like `2025-11-30-redis-experiment`
- **Native speed** - C binary, starts in milliseconds
- **Zero dependencies** - single statically-linked binary

## Quick Start

### Pre-built binaries (fastest)

```bash
# Linux x86_64
curl -sL https://github.com/tobi/try-cli/releases/latest/download/try-linux-x86_64.tar.gz | tar xz
sudo mv try /usr/local/bin/

# Linux aarch64
curl -sL https://github.com/tobi/try-cli/releases/latest/download/try-linux-aarch64.tar.gz | tar xz
sudo mv try /usr/local/bin/
```

> **macOS users**: Pre-built binaries for macOS are not currently available. Please use [Nix](#nix) or [build from source](#build-from-source) instead.

### Shell integration

```bash
# Add to your shell (bash/zsh)
echo 'eval "$(try init ~/src/tries)"' >> ~/.zshrc

# For fish shell users
echo 'eval (try init ~/src/tries | string collect)' >> ~/.config/fish/config.fish
```

### Build from source

```bash
git clone https://github.com/tobi/try-cli.git
cd try-cli
make
sudo make install

# Then add shell integration (see above)
```

## The Problem

You're learning Redis. You create `/tmp/redis-test`. Then `~/Desktop/redis-actually`. Then `~/projects/testing-redis-again`. Three weeks later you can't find that brilliant connection pooling solution you wrote at 2am.

## The Solution

All your experiments in one place, with instant fuzzy search:

```bash
$ try pool
→ 2025-11-28-redis-connection-pool    2h ago, 18.5
  2025-11-03-thread-pool              3d ago, 12.1
  2025-10-22-db-pooling               2w ago, 8.3
  + Create new: pool
```

Type, arrow down, enter. You're there.

## Features

### 🎯 Smart Fuzzy Search
Not just substring matching - it's smart:
- `rds` matches `redis-server`
- `connpool` matches `connection-pool`
- Recent stuff scores higher
- Shorter names win on equal matches

### ⏰ Time-Aware
- Shows how long ago you touched each project
- Recently accessed directories float to the top
- Perfect for "what was I working on yesterday?"

### 🎨 Pretty TUI
- Clean, minimal interface
- Highlights matches as you type
- Shows scores so you know why things are ranked
- Dark mode by default (because obviously)

### 📁 Organized Chaos
- Everything lives in `~/src/tries` (configurable via `--path` or `TRY_PATH`)
- Auto-prefixes with dates: `2025-11-30-your-idea`
- Skip the date prompt if you already typed a name

### Shell Integration

- Bash/Zsh:

  ```bash
  # Default is ~/src/tries
  eval "$(try init)"
  # Or pick a path
  eval "$(try init ~/code/experiments)"
  ```

- Fish:

  ```fish
  eval (try init | string collect)
  # Or pick a path
  eval (try init ~/code/experiments | string collect)
  ```

Notes:
- The runtime commands printed by `try` are shell-neutral (absolute paths, quoted). Only the small wrapper function differs per shell.

## Usage

```bash
try                                          # Browse all experiments
try redis                                    # Jump to redis experiment or create new
try clone https://github.com/user/repo.git  # Clone repo into date-prefixed directory
try https://github.com/user/repo.git        # Shorthand for clone (same as above)
try --help                                   # See all options
```

### Git Repository Cloning

**try** can automatically clone git repositories into properly named experiment directories:

```bash
# Clone with auto-generated directory name
try clone https://github.com/tobi/try.git
# Creates: 2025-11-30-try

# Clone with custom name
try clone https://github.com/tobi/try.git my-fork
# Creates: 2025-11-30-my-fork

# Shorthand syntax (no need to type 'clone')
try https://github.com/tobi/try.git
# Creates: 2025-11-30-try
```

Supported git URI formats:
- `https://github.com/user/repo.git` (HTTPS GitHub)
- `git@github.com:user/repo.git` (SSH GitHub)
- `https://gitlab.com/user/repo.git` (GitLab)
- `git@host.com:user/repo.git` (SSH other hosts)

The `.git` suffix is automatically removed from URLs when generating directory names.

### Keyboard Shortcuts

- `↑/↓` - Navigate
- `Enter` - Select or create
- `Backspace` - Delete character
- `ESC` - Cancel
- Just type to filter

## Configuration

Set `TRY_PATH` to change where experiments are stored:

```bash
export TRY_PATH=~/code/sketches
```

Or use the `--path` flag:

```bash
eval "$(try init ~/code/sketches)"
```

Default: `~/src/tries`

## Arch Linux

Install from the AUR using your preferred helper:

```bash
yay -S try-cli
# or
paru -S try-cli
```

Then add shell integration to your config (see [Shell Integration](#shell-integration) above).

## Nix

### Quick start

```bash
nix run github:tobi/try-cli
nix run github:tobi/try-cli -- --help
nix run github:tobi/try-cli -- init ~/my-tries
```

### Nix Flakes

```bash
nix profile install github:tobi/try-cli
```

### Home Manager

```nix
{
  inputs.try-cli.url = "github:tobi/try-cli";

  # Add to your home.nix or wherever you configure packages
  home.packages = [ inputs.try-cli.packages.${system}.default ];

  # Shell integration will be handled by your shell config
}
```

## Why C?

The [Ruby version](https://github.com/tobi/try) is excellent and perfectly usable. This C port exists for:

- **Speed** - Instant startup, even on slow hardware
- **Portability** - Single binary, no runtime dependencies
- **Systems without Ruby** - Works anywhere there's a C compiler
- **Fun** - Sometimes it's nice to write C

If you have Ruby installed and working, the [original](https://github.com/tobi/try) is totally fine. This version matches it feature-for-feature.

## The Philosophy

Your brain doesn't work in neat folders. You have ideas, you try things, you context-switch like a caffeinated squirrel. This tool embraces that.

Every experiment gets a home. Every home is instantly findable. Your 2am coding sessions are no longer lost to the void.

## FAQ

**Q: Why not just use `cd` and `ls`?**
A: Because you have 200 directories and can't remember if you called it `test-redis`, `redis-test`, or `new-redis-thing`.

**Q: Why not use `fzf`?**
A: fzf is great for files. This is specifically for project directories, with time-awareness and auto-creation built in.

**Q: Can I use this for real projects?**
A: You can, but it's designed for experiments. Real projects deserve real names in real locations.

**Q: What if I have thousands of experiments?**
A: First, welcome to the club. Second, it handles it fine - the scoring algorithm ensures relevant stuff stays on top.

**Q: Ruby version or C version?**
A: Both are maintained and feature-complete. Use Ruby if you have it. Use C if you want speed or don't have Ruby. Can't go wrong either way.

## Performance

- **Startup time**: < 5ms (vs ~100ms for Ruby)
- **Memory usage**: ~2MB (vs ~20MB for Ruby)
- **Binary size**: ~100KB (vs Ruby + stdlib)
- **Fuzzy search**: Same algorithm, same results

Both versions are plenty fast for interactive use. The C version just happens to be _really_ fast.

## Contributing

Pull requests welcome! This is a pretty straightforward C codebase:

```bash
git clone https://github.com/tobi/try-cli.git
cd try-cli
make          # Build
make test     # Run tests
./dist/try    # Try it out
```

See [CLAUDE.md](CLAUDE.md) for architecture details and development guidelines.

## License

MIT - Do whatever you want with it.

## Credits

- Original concept and Ruby implementation: [Tobias Lütke](https://github.com/tobi)
- C port maintains feature parity with the [Ruby version](https://github.com/tobi/try)
- Uses [z-libs](https://github.com/z-libs) for safe string/vector handling

---

*Built for developers with ADHD by developers with ADHD.*

*Your experiments deserve a home.* 🏠
