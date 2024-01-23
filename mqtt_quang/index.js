const mongoose = require('mongoose');
const mqtt = require('mqtt');
const  modelParam  = require('./model_schema');
require('dotenv').config();

const DB_URL = process.env.MONGO_URL;
const MQTT_BROKER_URL = 'mqtt://sanslab.viewdns.net:1883';

const options = {
  username: 'admin',
  password: '123',
};

// Kết nối MongoDB
mongoose.connect(DB_URL, { useNewUrlParser: true, useUnifiedTopology: true })
  .then(() => {
    console.log('Connected to MongoDB');
  })
  .catch((err) => {
    console.error('Error connecting to MongoDB:', err);
    process.exit(1);
  });

// Kết nối và lắng nghe dữ liệu từ MQTT
const mqttClient = mqtt.connect(MQTT_BROKER_URL, options);

mqttClient.on('connect', () => {
  console.log('Connected to MQTT broker');
  mqttClient.subscribe('#'); 
});

mqttClient.on('message', (topic, message) => {
  try {
    const data = JSON.parse(message.toString());

    // Kiểm tra topic và sử dụng model tương ứng
    if (topic.startsWith('modelParam')) {
      const modelData = new modelParam(data);
      saveData(modelData, 'modelParam');
    }
  } catch (error) {
    console.error('Error parsing MQTT message:', error);
  }
});

// Hàm lưu dữ liệu vào MongoDB
async function saveData(modelInstance, modelName) {
  try {
    await modelInstance.save();
    console.log(`Data saved to MongoDB (${modelName}):`, modelInstance);
  } catch (err) {
    console.error(`Error saving data to MongoDB (${modelName}):`, err);
  }
}

// Xử lý khi đóng kết nối
process.on('SIGINT', () => {
  mqttClient.end();
  mongoose.connection.close(() => {
    console.log('Disconnected from MongoDB');
    process.exit(0);
  });
});
