# C++ Static files Package via Docker to upload to Google Cloud.

This allows user to deploy dockerized application from mada server (local machine) onto cloud and deploy it.

## Prerequisites

Installs: Google Cloud SDK

Files:
- app.yaml
- Dockerfile
- Static Binary compiled locally - mada server.

```
$ gcloud init
$ Initialize: lgraph-220219
```

## Run

Run command in directory where all 3 files are located:
```
$ gcloud alpha builds submit . --config app.yaml
```

### Output

Visit Google Cloud Console. Outputs BUILD on Google Cloud Platform, and gives a pull image link.


