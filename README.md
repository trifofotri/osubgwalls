# osubgwalls

Download osu beatmap backgrounds!

## Building

You need libzip and libcurl.

```sh
cmake -B build && cmake --build build
```

## Usage

```sh
OSU_SESSION=$(cat session) ./osubgwalls [count]
```

You need to set OSU_SESSION to your osu session cookie, get it from the network tab or something idfk.