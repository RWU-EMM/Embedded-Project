TEST_CASES = [

{
    "name": "basic_led",
    "input": "PICO:LED:EN=1",
    "expected": {
        "token":"PICO",
        "led":{
            "en":1
        }
    }
},

{
    "name": "multi_field_led",
    "input": "PICO:LED:EN=1:BLINK=500",
    "expected": {
        "token":"PICO",
        "led":{
            "en":1,
            "blink_intvl":500
        }
    }
}

]