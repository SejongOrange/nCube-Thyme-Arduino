int infra1 = 5;
int infra2 = 6; // 적외선센서 5,6번에 연결
int vib = 9; // 진동센서 9번에 연결  
int echo = 10; 
int trig = 11; //초음파센서 10,11번에 연결  
int buz = 12; //부저 12번에 연결
 
void setup () {
  Serial.begin(9600);
  pinMode(infra1, INPUT);
  pinMode(infra2, INPUT);
  pinMode(vib,INPUT); 
  pinMode(echo, INPUT);
  pinMode(trig, OUTPUT);
  pinMode(buz,OUTPUT);
}
 
void loop () {
  int val1 = digitalRead(infra1);  // 적외선센서값 읽어옴
  int val2 = digitalRead(infra2);  //감지되면0,안되면1
  long measurement =TP_init();

  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  
  int distance =dis_init();
  Serial.print("distance = ");
  Serial.print(distance);
  Serial.println("cm");
  Serial.print("shock size = ");
  Serial.println(measurement);
  Serial.println(val1);
  Serial.println(val2);

}

int dis_init(){
  int distance = pulseIn(echo, HIGH) * 17 / 1000;
  return distance;
}

long TP_init(){
  long measurement=pulseIn (vib, HIGH); //진동을 감지해서 크기를 변수에 저장
  return measurement;
}
