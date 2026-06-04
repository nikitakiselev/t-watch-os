#!/usr/bin/env bash
#
# Собрать multi-arch Docker-образ сервера AI-ассистента и запушить в GitHub
# Container Registry (ghcr.io).
#
# Использование:
#   ./build-push.sh            # тег = короткий git sha + latest
#   ./build-push.sh v1.0       # тег = v1.0 + latest
#
# Конфиг через env (необязательно):
#   IMAGE      образ            (по умолчанию ghcr.io/nikitakiselev/twatch-assistant)
#   PLATFORMS  архитектуры      (по умолчанию linux/amd64,linux/arm64)
#   GHCR_TOKEN токен для login   (по умолчанию берётся `gh auth token`)
#   GH_USER    пользователь ghcr (по умолчанию nikitakiselev)
#
# Требования: docker + buildx, и либо `gh` (залогинен), либо GHCR_TOKEN.
# ВАЖНО: токену нужен scope write:packages. Токен из `gh` по умолчанию его не
# имеет — если login/push упадёт, выполни:
#   gh auth refresh -h github.com -s write:packages,read:packages
# либо создай classic PAT (write:packages) и положи в GHCR_TOKEN.

set -euo pipefail

IMAGE="${IMAGE:-ghcr.io/nikitakiselev/twatch-assistant}"
PLATFORMS="${PLATFORMS:-linux/amd64,linux/arm64}"
GH_USER="${GH_USER:-nikitakiselev}"
BUILDER="${BUILDER:-twatch-builder}"
CONTEXT="server"                      # каталог с Dockerfile

cd "$(dirname "$0")"                   # корень репозитория (скрипт лежит здесь)

TAG="${1:-$(git rev-parse --short HEAD 2>/dev/null || echo latest)}"

echo "==> image:     $IMAGE"
echo "==> tags:      $TAG, latest"
echo "==> platforms: $PLATFORMS"
echo "==> context:   $CONTEXT/"
echo

# ── 1. Логин в ghcr.io ──────────────────────────────────────────────────────
TOKEN="${GHCR_TOKEN:-$(gh auth token 2>/dev/null || true)}"
if [ -z "$TOKEN" ]; then
  echo "ERROR: нет токена. Задай GHCR_TOKEN (classic PAT со scope write:packages)" >&2
  echo "       или залогинься через gh (gh auth login)." >&2
  exit 1
fi
if ! echo "$TOKEN" | docker login ghcr.io -u "$GH_USER" --password-stdin; then
  echo >&2
  echo "ERROR: docker login ghcr.io не прошёл." >&2
  echo "Если токен от gh — у него может не быть scope write:packages. Выполни:" >&2
  echo "  gh auth refresh -h github.com -s write:packages,read:packages" >&2
  echo "или создай classic PAT (write:packages) и положи в GHCR_TOKEN." >&2
  exit 1
fi

# ── 2. Buildx builder с поддержкой multi-arch (docker-container driver) ──────
if ! docker buildx inspect "$BUILDER" >/dev/null 2>&1; then
  echo "==> создаю buildx builder '$BUILDER'"
  docker buildx create --name "$BUILDER" --driver docker-container --use >/dev/null
else
  docker buildx use "$BUILDER"
fi
docker buildx inspect --bootstrap >/dev/null

# ── 3. Сборка multi-arch + push ─────────────────────────────────────────────
docker buildx build \
  --builder "$BUILDER" \
  --platform "$PLATFORMS" \
  -t "$IMAGE:$TAG" \
  -t "$IMAGE:latest" \
  --push \
  "$CONTEXT"

echo
echo "==> готово: $IMAGE:$TAG (и :latest)"
echo "    pull:  docker pull $IMAGE:latest"
echo "    Пакет приватный по умолчанию — публичным делается в"
echo "    GitHub → ваш профиль → Packages → twatch-assistant → Package settings → Change visibility."
