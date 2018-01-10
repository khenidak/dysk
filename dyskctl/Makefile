binary := dyskctl

.PHONY: build
build: authors
	@echo "Building..."
	$Q cp -Rf cmd vendor/github.com/khenidak/dysk/dyskctl/
	$Q cp -Rf main.go vendor/github.com/khenidak/dysk/dyskctl/
	$Q cp -Rf ../pkg/client vendor/github.com/khenidak/dysk/pkg/
	$Q CGO_ENABLED=0 go build .

.PHONY: clean deps

deps:
	@echo "Ensuring Dependencies..."
	$Q go env
	$Q dep ensure

clean:
	@echo "Clean..."
	$Q rm -rf $(binary)

authors:
	$Q git log --all --format='%aN <%cE>' | sort -u  | sed -n '/github/!p' > GITAUTHORS
	$Q cat AUTHORS GITAUTHORS  | sort -u > NEWAUTHORS
	$Q mv NEWAUTHORS AUTHORS
	$Q rm -f NEWAUTHORS
	$Q rm -f GITAUTHORS
