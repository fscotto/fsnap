FROM docker.io/library/node:22-bookworm-slim AS node
FROM mcr.microsoft.com/devcontainers/base:bookworm
USER root
RUN apt-get update && apt-get install -y --no-install-recommends \
    bash-completion ca-certificates curl fzf git git-lfs gnupg openssh-client ripgrep zoxide build-essential clang clangd cmake gdb valgrind universal-ctags cscope \
    && rm -rf /var/lib/apt/lists/*
COPY --from=node /usr/local/bin/node /usr/local/bin/node
COPY --from=node /usr/local/lib/node_modules/npm/ /usr/local/lib/node_modules/npm/
RUN ln -s ../lib/node_modules/npm/bin/npm-cli.js /usr/local/bin/npm \
    && ln -s ../lib/node_modules/npm/bin/npx-cli.js /usr/local/bin/npx \
    && node --version && npm --version
RUN apt-get update \
    && arch="$(dpkg --print-architecture)" \
    && case "$arch" in \
      amd64) sha256=ea4f0222950ee750a3d38dd80d03bce4cee07a3f63928fc47548383bcaf23093 ;; \
      arm64) sha256=0edc36cf514f1bd84becac3e94ee8ae9f8818c6a1f99f7b2ee67b362afa253d3 ;; \
      *) echo "Unsupported architecture for delta: $arch" >&2; exit 1 ;; \
    esac \
    && curl -fsSL --retry 3 "https://github.com/dandavison/delta/releases/download/0.19.2/git-delta_0.19.2_${arch}.deb" -o /tmp/git-delta.deb \
    && echo "$sha256  /tmp/git-delta.deb" | sha256sum -c - \
    && apt-get install -y --no-install-recommends /tmp/git-delta.deb \
    && rm -rf /var/lib/apt/lists/* /tmp/git-delta.deb
RUN usermod --home '/home/fscotto' --move-home vscode
ENV HOME="/home/fscotto"
USER vscode
RUN git config --global include.path /run/host-gitconfig
