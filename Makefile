# Top-level convenience — real app targets live in app/
# CI/release workflows use workflow_dispatch; run `gh` from this directory.

CI_WORKFLOW := .github/workflows/ci.yml
RELEASE_WORKFLOW := .github/workflows/release.yml

# Always target origin (avoids gh ambiguity when upstream remote exists).
# Override: make ci REPO=nicapotato/demucs-mlx-arm-raylib-app
REPO ?= $(shell git remote get-url origin 2>/dev/null | sed -E 's|git@github\.com:||; s|https://github\.com/||; s|\.git$$||')
GH_R := $(if $(REPO),-R "$(REPO)",)

# git ref to run the workflow on (gh uses the default branch if omitted). Default = current branch.
# Your branch must be pushed to origin. Override: make ci REF=main
REF ?= $(shell git branch --show-current 2>/dev/null)

# CI / release toggles (workflow_dispatch inputs). Override e.g. PUBLISH_ITCH=false
PUBLISH_ITCH ?= true
PUBLISH_GH_RELEASE ?= true

.PHONY: run build bundle verify-mp3 verify-psarc models worker clean \
	ci ci-watch release release-watch

run build bundle verify-mp3 verify-psarc models worker clean:
	$(MAKE) -C app $@

# Dispatch GitHub Actions. Requires: gh (https://cli.github.com/), authenticated.
# CI: python tests + macOS bundle + itch.io (no git tag / GitHub Release).
# Examples:
#   make ci
#   make ci REF=main
#   make ci PUBLISH_ITCH=false
#   make ci VERSION=0.1.1
#   make ci-watch
ci:
	@test -n "$(REPO)" || (echo "ERROR: could not resolve origin repo; set REPO=owner/name" >&2; exit 1)
	gh $(GH_R) workflow run "$(CI_WORKFLOW)" \
		$(if $(REF),-r "$(REF)",) \
		-f publish_itch="$(PUBLISH_ITCH)" \
		$(if $(VERSION),-f version="$(VERSION)",)

# Full release: bundle + itch + GitHub Release (version from project.conf unless VERSION=).
# Examples:
#   make release
#   make release REF=main
#   make release VERSION=0.1.1
#   make release PUBLISH_ITCH=true PUBLISH_GH_RELEASE=false
release:
	@test -n "$(REPO)" || (echo "ERROR: could not resolve origin repo; set REPO=owner/name" >&2; exit 1)
	gh $(GH_R) workflow run "$(RELEASE_WORKFLOW)" \
		$(if $(REF),-r "$(REF)",) \
		-f publish_itch="$(PUBLISH_ITCH)" \
		-f publish_gh_release="$(PUBLISH_GH_RELEASE)" \
		$(if $(VERSION),-f version="$(VERSION)",)

# Dispatch then attach to the newest run log (same workflow file).
ci-watch: ci
	@sleep 2
	@RID=$$(gh $(GH_R) run list --workflow="$(CI_WORKFLOW)" -L 1 --json databaseId -q '.[0].databaseId'); \
		test -n "$$RID"; \
		gh $(GH_R) run watch "$$RID"

release-watch: release
	@sleep 2
	@RID=$$(gh $(GH_R) run list --workflow="$(RELEASE_WORKFLOW)" -L 1 --json databaseId -q '.[0].databaseId'); \
		test -n "$$RID"; \
		gh $(GH_R) run watch "$$RID"
