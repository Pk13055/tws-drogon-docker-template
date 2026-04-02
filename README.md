# Lightweight C++ TWS API Drogon wrapper Template

## Docker image

Pre-built images are on [Docker Hub: `pk13055/tws-drogon-docker-template`](https://hub.docker.com/repository/docker/pk13055/tws-drogon-docker-template/).

### Using as a base image

Use the published image as a parent in your own `Dockerfile` when you want Drogon, AMQP-CPP, and the IBKR TWS C++ SDK already installed:

```dockerfile
FROM pk13055/tws-drogon-docker-template:latest
# Add your application source, CMake layout, or extra packages here
```

Pull directly: `docker pull pk13055/tws-drogon-docker-template:latest`

### Linking GitHub and Docker Hub

- **From Docker Hub to this repo:** In the Docker Hub repository **Settings** or overview, set the description or full description to include the [GitHub repository](https://github.com/Pk13055/tws-drogon-docker-template) URL so users can jump to source.
- **From GitHub to Docker Hub:** In the GitHub repo **About** section, set **Website** (or add a link in the readme) to the [Docker Hub repository](https://hub.docker.com/repository/docker/pk13055/tws-drogon-docker-template/) page.

## Instructions

This is a simple template docker container, best used with a docker compose stack that has a [dockerized tws service](https://github.com/extrange/ibkr-docker) exposing the TWS API on 8888.
You can change this port to accomodate it for other use cases as well. This template already comes bundled with rabbitmq support as well.
