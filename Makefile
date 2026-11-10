DOCKER ?= docker
IMAGE ?= mlab-dev

.PHONY: test image

image:
	$(DOCKER) build -t $(IMAGE) .

test: image
	$(DOCKER) run --rm --user "$(shell id -u):$(shell id -g)" -v "$(CURDIR):/workspace" -w /workspace $(IMAGE) cmake --preset debug
	$(DOCKER) run --rm --user "$(shell id -u):$(shell id -g)" -v "$(CURDIR):/workspace" -w /workspace $(IMAGE) cmake --build --preset debug --parallel 2
	$(DOCKER) run --rm --user "$(shell id -u):$(shell id -g)" -v "$(CURDIR):/workspace" -w /workspace $(IMAGE) ctest --preset debug