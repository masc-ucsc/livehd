exports.handler = function(event, context, callback) {
  console.log('AWS CLI - Hello World');
  callback(null, "success");
};
