const mongoose = require('mongoose')

const modelSchema = new mongoose.Schema({
    Time_mqtt_Date:{
        type:Date,
        default: Date.now(),
    },
    Time_real_Date:{
        type:String,
        default: 0,
    },
    humidity:{
        type: Number,
        default: 0
    },
    temperature:{
        type: Number,
        default: 0
    },
    CO2:{
        type: Number,
        default: 0
    },
    CO:{
        type: Number,
        default: 0
    },
})
module.exports = mongoose.model('modelParam',modelSchema)




