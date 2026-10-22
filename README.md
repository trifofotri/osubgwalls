# osubgwalls

*Download osu beatmap backgrounds!*

<img width="595" height="421" alt="image" src="https://github.com/user-attachments/assets/22aeb76a-81ec-4726-9cfc-bd79556eb1d2" />

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
