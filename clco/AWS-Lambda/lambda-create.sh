#!/bin/bash

aws lambda create-function \
  --function-name "helloworld" \
  --runtime "nodejs6.10" \
  --role "arn:aws:iam::[ROLE ID]:role/Lambda-Role-FullAccess" \
  --handler "/app/index.handler" \
  --timeout 5 \
  --zip "fileb://./app.zip" \
  --region "us-west-2"

``
