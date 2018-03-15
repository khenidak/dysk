binary := dyskctl

all: clean deps build authors

.PHONY: build
build: ## Builds the `dyskctl` executable
	@echo "Building..."
	$Q cp -Rf cmd vendor/github.com/khenidak/dysk/dyskctl/
	$Q cp -Rf main.go vendor/github.com/khenidak/dysk/dyskctl/
	$Q cp -Rf ../pkg/client vendor/github.com/khenidak/dysk/pkg/
	$Q CGO_ENABLED=0 go build .

.PHONY: deps
deps: ## Runs `dep ensure`
	@echo "Ensuring Dependencies..."
	$Q go env
	$Q dep ensure

.PHONY: clean
clean: ## Cleanup any build binaries
	@echo "Clean..."
	$Q rm -rf $(binary)

.PHONY: fmt
fmt: ## Verifies all files have been `gofmt`ed
	@echo "+ $@"
	@gofmt -s -l . | grep -v vendor | tee /dev/stderr

.PHONY: lint
lint: ## Verifies `golint` passes
	@echo "+ $@"
	@golint ./... | grep -v vendor | tee /dev/stderr	

.PHONY: authors
authors:
	$Q git log --all --format='%aN <%cE>' | sort -u  | sed -n '/github/!p' > GITAUTHORS
	$Q cat AUTHORS GITAUTHORS  | sort -u > NEWAUTHORS
	$Q mv NEWAUTHORS AUTHORS
	$Q rm -f NEWAUTHORS
	$Q rm -f GITAUTHORS

.PHONY: help
help: ## Prints out this help information
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'